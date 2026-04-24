//> includes
#include "vk_engine.h"
#define VMA_IMPLEMENTATION
#include "vk_mem_alloc.h"

#include <SDL.h>
#include <SDL_vulkan.h>

#include <vk_initializers.h>
#include <vk_types.h>

#include "VkBootstrap.h"

#include <chrono>
#include <thread>
#include "vk_images.h"

#include <vk_pipelines.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_vulkan.h"

#include "utils.h"

ComputePushConstants g_PushConstant;

VulkanEngine* loadedEngine = nullptr;

VulkanEngine& VulkanEngine::Get() { return *loadedEngine; }
void VulkanEngine::Init()
{
    // only one engine initialization is allowed with the application.
    assert(loadedEngine == nullptr);
    loadedEngine = this;

    // We initialize SDL and create a window with it.
    SDL_Init(SDL_INIT_VIDEO);

    SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_VULKAN);

    m_Window = SDL_CreateWindow(
        "Vulkan Engine",
        SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED,
        m_WindowExtent.width,
        m_WindowExtent.height,
        window_flags);
    
  

    InitVulkan();

    InitSwapchain();

    InitBuffers();

    InitCommands();

    InitSyncStructures();

    
    ImageProperties prop = {};
    prop.width = m_WindowExtent.width;
    prop.height = m_WindowExtent.height;
    prop.mipLevels = 1;
    prop.tiling = VK_IMAGE_TILING_OPTIMAL;
    prop.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT  | VK_IMAGE_USAGE_SAMPLED_BIT;
    prop.format = VK_FORMAT_R32G32B32A32_SFLOAT;
    
    m_UnitRandomVectorTexture = CreateTexture(prop, nullptr);
    
    InitDescriptors();
    
	InitPipelines();

	InitProfilingTools();

	InitImgui();
    
    InitScene();
    
    // everything went fine
    m_bIsInitialized = true;
}

void VulkanEngine::Cleanup()
{
    if (m_bIsInitialized)
    {
        vkDeviceWaitIdle(m_GPUDeviceHandle);

        for (int i = 0; i < FRAME_MAX; i++)
        {
            vkDestroyCommandPool(m_GPUDeviceHandle, m_Frames[i].commandPool, nullptr);

            vkDestroyFence(m_GPUDeviceHandle, m_Frames[i].renderFence, nullptr);
            vkDestroySemaphore(m_GPUDeviceHandle, m_Frames[i].swapchainSemaphore, nullptr);
            vkDestroySemaphore(m_GPUDeviceHandle, m_Frames[i].renderSemaphore, nullptr);

            m_Frames[i].deletionQueue.Flush();
        }

        globalDeletionQueue.Flush();

        DestroySwapchain();

        vkDestroySurfaceKHR(m_Instance, m_WindowSurface, nullptr);
        vkDestroyDevice(m_GPUDeviceHandle, nullptr);
        
        vkb::destroy_debug_utils_messenger(m_Instance, m_DebugMessenger);
        vkDestroyInstance(m_Instance, nullptr);
        SDL_DestroyWindow(m_Window);
    }



    // clear engine pointer
    loadedEngine = nullptr;
}

void VulkanEngine::UpdateUBO()
{
    m_Frames[m_CurrentFrame].ubo.m_ComputeUniformBuffer = m_SceneData;


    memcpy((char*)m_Frames[m_CurrentFrame].ubo.m_UniformBufferCPUMappedMemory, &m_Frames[m_CurrentFrame].ubo.m_ComputeUniformBuffer, sizeof(m_Frames[m_CurrentFrame].ubo.m_ComputeUniformBuffer));
}

void VulkanEngine::Draw()
{
    static bool firstFrame = true;

    //wait for gpu rendering previous frame
    VK_CHECK(vkWaitForFences(m_GPUDeviceHandle, 1, &GetCurrentFrameContext().renderFence, true, uint64_t(-1)));
    
    //we wait last frame to finish rendering and then measure to check the difference
    if (!firstFrame)
    {
	    uint64_t timestamps[2];
         vkGetQueryPoolResults(m_GPUDeviceHandle, m_QueryPool, 0, 2,
         sizeof(timestamps), timestamps,
         sizeof(uint64_t),
         VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
     
	    uint64_t t0 = timestamps[0];
	    uint64_t t1 = timestamps[1];
        m_LastFrameTime = double(t1 - t0) * m_TimestampPeriod / 1000.0f / 1000.0f;
    }

    GetCurrentFrameContext().deletionQueue.Flush();

    VK_CHECK(vkResetFences(m_GPUDeviceHandle, 1, &GetCurrentFrameContext().renderFence));

    m_DrawExtent.width  = m_DrawImage.extent.width;
    m_DrawExtent.height = m_DrawImage.extent.height;

    //block the thread until we finished to present and free a render target to draw
    uint32_t swapchainImageIndex;
    VK_CHECK(vkAcquireNextImageKHR(m_GPUDeviceHandle, m_Swapchain, uint64_t(-1), GetCurrentFrameContext().swapchainSemaphore, nullptr, &swapchainImageIndex));

    VkCommandBuffer cmdBuffer = GetCurrentFrameContext().commandBuffer;

    //Render fence guarantee us that gpu has done consuming this framebuffer
    VK_CHECK(vkResetCommandBuffer(cmdBuffer, 0));

    //We are going to use it only once and then reset it for the next frame (this is different as having a pool for one-time buffers, as we would submit those only once but not ever reset them after)
    VkCommandBufferBeginInfo cmdBufferBeginInfo = vkinit::command_buffer_begin_info(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

   //start recording
    VK_CHECK(vkBeginCommandBuffer(cmdBuffer, &cmdBufferBeginInfo));

	vkCmdResetQueryPool(cmdBuffer, m_QueryPool, 0, 2);
	vkCmdWriteTimestamp(cmdBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, m_QueryPool, 0);

    //discard old contents, transition temporary image
    vkutil::TransitionImage(cmdBuffer, m_DrawImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

    //Update from CPU stuff for the current queued frame 
    g_PushConstant.currentFrame = m_CurrentFrame;
    UpdateUBO();
    DrawBackground(cmdBuffer);

    vkutil::TransitionImage(cmdBuffer, m_DrawImage.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    vkutil::TransitionImage(cmdBuffer, m_SwapchainImages[swapchainImageIndex], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    
    vkutil::BlitImage(cmdBuffer, m_DrawImage.image, m_SwapchainImages[swapchainImageIndex], m_DrawExtent, m_SwapchainExtent);
   
    vkutil::TransitionImage(cmdBuffer, m_SwapchainImages[swapchainImageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    DrawImGui(cmdBuffer, m_SwapchainImageViews[swapchainImageIndex]);

    vkutil::TransitionImage(cmdBuffer, m_SwapchainImages[swapchainImageIndex], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
 
    vkCmdWriteTimestamp(cmdBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, m_QueryPool, 1);
    VK_CHECK(vkEndCommandBuffer(cmdBuffer));

    VkCommandBufferSubmitInfo cmdBufferSubmitInfo = vkinit::command_buffer_submit_info(cmdBuffer);

    VkSemaphoreSubmitInfo waitInfo = vkinit::semaphore_submit_info(VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR, GetCurrentFrameContext().swapchainSemaphore);
    VkSemaphoreSubmitInfo signalInfo = vkinit::semaphore_submit_info(VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, GetCurrentFrameContext().renderSemaphore);

    //wait on swapchain semaphore until we have a valid image to render to
    //then when finishing this command, signals internally (gpu) that our render is over (renderSemaphore)
    VkSubmitInfo2 submission = vkinit::submit_info(&cmdBufferSubmitInfo, &signalInfo, &waitInfo);

    //when the queue is over, all rendering commands are finished, so tells the cpu to continue (it will block on the wait fence until we signaled this)
    VK_CHECK(vkQueueSubmit2(m_GraphicsQueue, 1, &submission, GetCurrentFrameContext().renderFence));

    VkPresentInfoKHR presentInfo = {};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.pNext = nullptr;
    presentInfo.pSwapchains = &m_Swapchain;
    presentInfo.swapchainCount = 1;

    presentInfo.pWaitSemaphores = &GetCurrentFrameContext().renderSemaphore;
    presentInfo.waitSemaphoreCount = 1;

    presentInfo.pImageIndices = &swapchainImageIndex;

    VK_CHECK(vkQueuePresentKHR(m_GraphicsQueue, &presentInfo));

    m_FrameCount++;
    m_CurrentFrame = m_FrameCount % FRAME_MAX;
    firstFrame = false;
}


void VulkanEngine::DrawBackground(VkCommandBuffer cmd)
{
	VkClearColorValue clearValue;
	float flash = std::abs(std::sin(m_FrameCount / 120.0f));
	clearValue = { { 0.0f, 0.0f, flash, 1.0f } };

    VkImageSubresourceRange clearRange = vkinit::image_subresource_range(VK_IMAGE_ASPECT_COLOR_BIT);

    //clear image
    vkCmdClearColorImage(cmd, m_DrawImage.image, VK_IMAGE_LAYOUT_GENERAL, &clearValue, 1, &clearRange);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, sandboxPipeline);

    //bind descriptor cointaing draw image
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, sandboxPipelineLayout, 0, 1, &m_DrawImageDescriptors, 0, nullptr);

    vkCmdPushConstants(cmd, sandboxPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(ComputePushConstants), &g_PushConstant);

    //we divide our screen by 16.0f because each shader workgroup is 16x16 so this is the invocation number we need.
    vkCmdDispatch(cmd, std::ceil(m_DrawExtent.width / 16.0f), std::ceil(m_DrawExtent.height / 16.0f), 1.0);

}

void VulkanEngine::ProcessInput(const SDL_Keysym& input)
{
    SDL_Keycode key = input.sym;

	if (key == SDLK_ESCAPE)
	{
		SDL_Event quitEvent;
		quitEvent.type = SDL_QUIT;
		SDL_PushEvent(&quitEvent);
	}

    glm::vec4 position(0.0f, 0.0f, 0.0f, 0.0f);


    if (key == SDLK_w)
    {
        position.z -= 0.1f;
    }

    if(key == SDLK_a)
    {
        position.x -= 0.1f;
    }

	if (key == SDLK_s)
	{
        position.z += 0.1f;
	}

	if (key == SDLK_d)
	{
        position.x += 0.1f;
	}

	if (key == SDLK_SPACE)
	{
		position.y += 0.1f;
	}

	if (key == SDLK_LCTRL)
	{
		position.y -= 0.1f;
	}

    AddCameraTargetPosition(position);

	if (key == SDLK_F1)
	{
		m_MouseLocked = !m_MouseLocked;
		SDL_SetRelativeMouseMode((SDL_bool)m_MouseLocked);
		SDL_SetWindowGrab(m_Window, (SDL_bool)m_MouseLocked);
	}
}

void VulkanEngine::ProcessMouse(const SDL_MouseMotionEvent& input)
{
    if (!m_MouseLocked)
        return;

    static bool firstMouse = true;


	if (firstMouse)
	{
        m_LastMouse.x = input.x;
        m_LastMouse.y = input.y;
		firstMouse = false;
	}

	float xoffset = input.x - m_LastMouse.x;
	float yoffset = input.y - m_LastMouse.y;
    
    m_LastMouse.x = input.x;
    m_LastMouse.y = input.y;

	float sensitivity = 0.01f;
	xoffset *= sensitivity;
	yoffset *= sensitivity;

    m_SceneData.cameraData.yawPitch.x += xoffset;
    m_SceneData.cameraData.yawPitch.y += yoffset;
}



AllocatedImage VulkanEngine::CreateTexture(const ImageProperties& properties, uint8_t* data)
{

    ////////////// GPU local image
    AllocatedImage allocatedImage = {};

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = properties.width;
    imageInfo.extent.height = properties.height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = properties.mipLevels;
    imageInfo.arrayLayers = 1;
    imageInfo.format = properties.format;
    imageInfo.tiling = properties.tiling;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; // we'll create this image just to fill it later with our staging buffer
    imageInfo.usage = properties.usage;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    

     VkExtent3D drawImageExtent = { properties.width, properties.height, 1 };
    //allocatedImage.format = VK_FORMAT_R16G16B16A16_SFLOAT;
//    drawImageUsages = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
     VkImageCreateInfo drawImageCreateImgInfo = {};
    drawImageCreateImgInfo = imageInfo;
    drawImageCreateImgInfo = vkinit::image_create_info(imageInfo.format, imageInfo.usage, imageInfo.extent);
    drawImageCreateImgInfo.arrayLayers = 1;
    drawImageCreateImgInfo.mipLevels = 1;
    drawImageCreateImgInfo.samples = VK_SAMPLE_COUNT_1_BIT;

    drawImageCreateImgInfo.initialLayout = imageInfo.initialLayout;
    //#TODO turn this into a staging and transfer to a local only memory without host visibility
    VmaAllocationCreateInfo drawImageAllocInfo = {};
    drawImageAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    drawImageAllocInfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VK_CHECK(vmaCreateImage(m_Allocator, &drawImageCreateImgInfo, &drawImageAllocInfo, &allocatedImage.image, &allocatedImage.allocation, nullptr));

	globalDeletionQueue.PushCallback([=]() {
		vkDestroyImageView(m_GPUDeviceHandle, allocatedImage.view, nullptr);
		vmaDestroyImage(m_Allocator, allocatedImage.image, allocatedImage.allocation);
		});

    VkCommandBuffer commandBuffer = BeginSingleSubmissionCommands();

    vkutil::TransitionImage(commandBuffer, allocatedImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    ////////////// GPU local image

    ////////////// Staging
    //CreateInternalBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, textureStagingBuffer, textureStagingBufferMemory);
    VkBufferCreateInfo bufferInfo = {};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = properties.width * properties.height * 16;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT; //this will be used only as a source for the image
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo stagingCreateAllocInfo = {};
    stagingCreateAllocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
    stagingCreateAllocInfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    //VK_CHECK(vmaCreateBuffer(m_Allocator, &bufferInfo, &vmaAllocInfo, &m_Frames[i].ubo.m_UniformBuffer, &m_Frames[i].ubo.m_UniformBufferAlloc, nullptr));
    VkBuffer stagingBuffer = {};
    VmaAllocation stagingAlloc = {};
    VmaAllocationInfo stagingAllocInfo = {};
    VK_CHECK(vmaCreateBuffer(m_Allocator, &bufferInfo, &stagingCreateAllocInfo, &stagingBuffer, &stagingAlloc, nullptr));
    void* stagingCPUPtr = nullptr;
    VK_CHECK(vmaMapMemory(m_Allocator, stagingAlloc, &stagingCPUPtr));

    std::vector<glm::vec4> fibPoints;
    fibPoints.reserve(properties.width * properties.height);

    for (int x = 0; x < properties.width; x++)
    {
        for (int y = 0; y < properties.height; y++)
        {
            glm::vec3 random = glm::normalize(CreateRandomVectors());
            glm::vec4 fib = glm::vec4(random.x, random.y, random.z, 1.0f);
            fibPoints.push_back(fib);
        }
    }

    memcpy(stagingCPUPtr, &fibPoints[0], sizeof(glm::vec4) * fibPoints.size());
    vmaUnmapMemory(m_Allocator, stagingAlloc);
    ////////////// Staging



    VkBufferImageCopy region = {};

    //byte offset in the buffer of the pixels to begin
    region.bufferOffset = 0;

    //The bufferRowLength and bufferImageHeight fields specify how the pixels are laid out in memory.
    //For example, you could have some padding bytes between rows of the image.
    //Specifying 0 for both indicates that the pixels are simply tightly packed like they are in our case.
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;


    //part of the image that we want to copy
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;

    region.imageOffset = { 0, 0, 0 };
    region.imageExtent = { properties.width, properties.height, 1 };

    vkCmdCopyBufferToImage(commandBuffer, stagingBuffer, allocatedImage.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    vkutil::TransitionImage(commandBuffer, allocatedImage.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    EndSingleSubmissionCommands(commandBuffer);


   //view --------------------------------------
    VkImageViewCreateInfo viewInfo = {};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = allocatedImage.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = properties.format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;
    


    VK_CHECK(vkCreateImageView(m_GPUDeviceHandle, &viewInfo, nullptr, &allocatedImage.view));
    //view --------------------------------------

   //sampler --------------------------------------

	VkSamplerCreateInfo samplerInfo = {};

	samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_NEAREST;
	samplerInfo.minFilter = VK_FILTER_NEAREST;

	//samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	//samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	//samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;

	samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE ;
	samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;


    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;

	//If we want sample the texture using [0, texWidth] and [0, textHeight] or [0, 1] for both
	//samplerInfo.unnormalizedCoordinates = VK_FALSE;
	samplerInfo.unnormalizedCoordinates = VK_TRUE;
	samplerInfo.compareEnable = VK_FALSE;
	
    //samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
	samplerInfo.minLod = 0;
    samplerInfo.maxLod = 0;
	samplerInfo.mipLodBias = 0.0f;

    VK_CHECK(vkCreateSampler(m_GPUDeviceHandle, &samplerInfo, nullptr, &allocatedImage.sampler));

   //sampler --------------------------------------

    vmaDestroyBuffer(m_Allocator, stagingBuffer, stagingAlloc);
    

    return allocatedImage;
}

glm::vec3 VulkanEngine::CreateRandomVectors()
{
    return Utils::RandomInUnitSphere();
	
}

void VulkanEngine::UpdateCamera()
{
    glm::mat4 cameraRot(1.0f), cameraPos(1.0f);

    Camera& camera = m_SceneData.cameraData;

	cameraRot = glm::rotate(cameraRot, camera.yawPitch.y, glm::vec3(1.0f, 0.0f, 0.0f));
	cameraRot = glm::rotate(cameraRot, camera.yawPitch.x, glm::vec3(0.0f, 1.0f, 0.0f));

    glm::vec3 pos3 = glm::vec3(camera.position.x, camera.position.y, camera.position.z);
    cameraPos = glm::translate(cameraPos, pos3);

    glm::mat4 viewMatrix = cameraRot * cameraPos;

	glm::vec4 forward(viewMatrix[0][2], viewMatrix[1][2], viewMatrix[2][2], 1.0f);
	glm::vec4 strafe(viewMatrix[0][0], viewMatrix[1][0], viewMatrix[2][0], 1.0f);
	glm::vec4 up(viewMatrix[0][1], viewMatrix[1][1], viewMatrix[2][1], 1.0f);

    camera.targetDir = forward;
    camera.right = strafe;
    camera.up = up;
}

void VulkanEngine::AddCameraTargetPosition(glm::vec4 pos)
{
    Camera& camera = m_SceneData.cameraData;

	glm::vec4 target(0.0f, 0.0f, 0.0f, 1.0f);
	target += ((camera.targetDir * pos.z) + (camera.right * pos.x)) + (camera.up * pos.y);
    camera.position += target;
}

void VulkanEngine::Run()
{
    SDL_Event e;
    bool bQuit = false;

    // main loop
    while (!bQuit) {
        // Handle events on queue
        while (SDL_PollEvent(&e) != 0) {
            // close the window when user alt-f4s or clicks the X button
            if (e.type == SDL_QUIT)
                bQuit = true;

            if (e.type == SDL_WINDOWEVENT) 
            {
                if (e.window.event == SDL_WINDOWEVENT_MINIMIZED) {
                    m_bStopRendering = true;
                }
                if (e.window.event == SDL_WINDOWEVENT_RESTORED) {
                    m_bStopRendering = false;
                }
            }

            if (e.type == SDL_KEYDOWN)
            {
                ProcessInput(e.key.keysym);
            }

            if (e.type == SDL_MOUSEMOTION)
            {
                ProcessMouse(e.motion);
            }

            ImGui_ImplSDL2_ProcessEvent(&e);
        }

		// do not draw if we are minimized
		if (m_bStopRendering) {
			// throttle the speed to avoid the endless spinning
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
			continue;
		}

        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        UpdateUIMenus();

        ImGui::Render();

        UpdateCamera();

        Draw();
    }
}


void VulkanEngine::ImmediateSubmit(std::function<void(VkCommandBuffer cmd)>&& function)
{
    VK_CHECK(vkResetFences(m_GPUDeviceHandle, 1, &m_ImmediateFence));
    VK_CHECK(vkResetCommandBuffer(m_ImmediateCommandBuffer, 0));

    VkCommandBufferBeginInfo cmdBeginInfo = vkinit::command_buffer_begin_info(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    
    VK_CHECK(vkBeginCommandBuffer(m_ImmediateCommandBuffer, &cmdBeginInfo));

    //invoke
    function(m_ImmediateCommandBuffer);

    VK_CHECK(vkEndCommandBuffer(m_ImmediateCommandBuffer));

    VkCommandBufferSubmitInfo cmdSubInfo = vkinit::command_buffer_submit_info(m_ImmediateCommandBuffer);
    VkSubmitInfo2 submitInfo = vkinit::submit_info(&cmdSubInfo, nullptr, nullptr);

    VK_CHECK(vkQueueSubmit2(m_GraphicsQueue, 1, &submitInfo, m_ImmediateFence));
    
    VK_CHECK(vkWaitForFences(m_GPUDeviceHandle, 1, &m_ImmediateFence, true, (uint64_t)-1));

}


void VulkanEngine::DrawImGui(VkCommandBuffer cmd, VkImageView targetImageView)
{
    VkRenderingAttachmentInfo colorAttachment = vkinit::attachment_info(targetImageView, nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    VkRenderingInfo renderInfo = vkinit::rendering_info(m_SwapchainExtent, &colorAttachment, nullptr);

    vkCmdBeginRendering(cmd, &renderInfo);

    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);

    vkCmdEndRendering(cmd);
}

void VulkanEngine::InitVulkan()
{
    //init instance
    vkb::InstanceBuilder builder;

    auto instRet = builder.set_app_name("VkCompute")
        .request_validation_layers(bUseValidationLayers)
        .use_default_debug_messenger()
        .require_api_version(1, 3, 0).build();

    vkb::Instance vkbInst = instRet.value();

    m_Instance = vkbInst.instance;
    m_DebugMessenger = vkbInst.debug_messenger;


    //init device
    SDL_Vulkan_CreateSurface(m_Window, m_Instance, &m_WindowSurface);

    //1.3 features
    VkPhysicalDeviceVulkan13Features features13 = {};
    features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    features13.dynamicRendering = true;
    features13.synchronization2 = true;
    
    //1.2 features
    VkPhysicalDeviceVulkan12Features features12 = {};
    features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    features12.bufferDeviceAddress = true;
    features12.descriptorIndexing = true;
    
    

    //select gpu
    vkb::PhysicalDeviceSelector selector{ vkbInst };
    
    vkb::PhysicalDevice physicalDevice = selector.set_minimum_version(1, 4).set_required_features_13(features13).set_required_features_12(features12).set_surface(m_WindowSurface).select().value();
    
    //create device
    vkb::DeviceBuilder deviceBuilder(physicalDevice);
    vkb::Device vkbDevice = deviceBuilder.build().value();
    m_TimestampPeriod = vkbDevice.physical_device.properties.limits.timestampPeriod;

    m_GPUDeviceHandle = vkbDevice.device;
    m_GPU = physicalDevice.physical_device;

    //Init queue
    m_GraphicsQueue = vkbDevice.get_queue(vkb::QueueType::graphics).value();
    m_GraphicsQueueType = vkbDevice.get_queue_index(vkb::QueueType::graphics).value();

    VmaAllocatorCreateInfo allocatorInfo = {};
    allocatorInfo.physicalDevice = m_GPU;
    allocatorInfo.device = m_GPUDeviceHandle;
    allocatorInfo.instance = m_Instance;
    allocatorInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
    
    vmaCreateAllocator(&allocatorInfo, &m_Allocator);

    //add deletion function callback
    globalDeletionQueue.PushCallback([&]() { vmaDestroyAllocator(m_Allocator); });


}

void VulkanEngine::InitScene()
{
    m_SceneData.m_Materials[0] = {
        .attenuation =  glm::vec4(0.4f, 0.4f, 0.8f, 1.0f),
        .roughness = 0.0f,
        .refractionIdx =  0.0f,
        .type = 0 };
    
    m_SceneData.m_Materials[1] = {
     .attenuation = glm::vec4(1.0f, 1.0f, 0.0f, 1.0f),
     .roughness = 0.0f,
     .refractionIdx = 0.0f,
     .type = 1 };
    
    m_SceneData.m_Materials[2] = {
     .attenuation = glm::vec4(1.0f, 1.0f, 0.0f, 1.0f),
     .roughness = 0.0f,
     .refractionIdx = 1.5f,
     .type = 2 };
    
    m_SceneData.m_Spheres[0] = {
        .centerRadius = glm::vec4(0.0f, 0.0f, 0.0f, 0.5f),
        .materialType = 1};
    
    m_SceneData.m_Spheres[1] = {
     .centerRadius = glm::vec4(0.0f, -100.5f, 0.0f, 100.0f),
        .materialType = 0 };
    
    m_SceneData.m_SpheresCount = 2;
}

void VulkanEngine::InitSwapchain()
{
    CreateSwapchain(m_WindowExtent.width, m_WindowExtent.height);

    //draw image size will match the window
    VkExtent3D drawImageExtent = { m_WindowExtent.width, m_WindowExtent.height, 1 };

    m_DrawImage.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    m_DrawImage.extent = drawImageExtent;

    //VK_IMAGE_USAGE_STORAGE_BIT is just so that the compute shader can write to it
    VkImageUsageFlags drawImageUsages = {};
    drawImageUsages = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    VkImageCreateInfo drawImageCreateImgInfo = vkinit::image_create_info(m_DrawImage.format, drawImageUsages, drawImageExtent);

    //alocate on gpu memory
    VmaAllocationCreateInfo drawImageAllocInfo = {};
    //to be out of upload heap region 
    drawImageAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    drawImageAllocInfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    //allocate, create and store image
    vmaCreateImage(m_Allocator, &drawImageCreateImgInfo, &drawImageAllocInfo, &m_DrawImage.image, &m_DrawImage.allocation, nullptr);

    VkImageViewCreateInfo drawImageViewCreateInfo = vkinit::imageview_create_info(m_DrawImage.format, m_DrawImage.image, VK_IMAGE_ASPECT_COLOR_BIT);

    VK_CHECK(vkCreateImageView(m_GPUDeviceHandle, &drawImageViewCreateInfo, nullptr, &m_DrawImage.view));

    globalDeletionQueue.PushCallback([=]() {
        vkDestroyImageView(m_GPUDeviceHandle, m_DrawImage.view, nullptr);
        vmaDestroyImage(m_Allocator, m_DrawImage.image, m_DrawImage.allocation);
        });
}

void VulkanEngine::InitCommands()
{
    VkCommandPoolCreateInfo commandPoolInfo = vkinit::command_pool_create_info(m_GraphicsQueueType, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);

    for (int i = 0; i < FRAME_MAX; i++)
    {
        VK_CHECK(vkCreateCommandPool(m_GPUDeviceHandle, &commandPoolInfo, nullptr, &m_Frames[i].commandPool));

        VkCommandBufferAllocateInfo cmdAllocInfo = vkinit::command_buffer_allocate_info(m_Frames[i].commandPool, 1);

        VK_CHECK(vkAllocateCommandBuffers(m_GPUDeviceHandle, &cmdAllocInfo, &m_Frames[i].commandBuffer));
    }

    VK_CHECK(vkCreateCommandPool(m_GPUDeviceHandle, &commandPoolInfo, nullptr, &m_ImmediateCommandPool));
    VkCommandBufferAllocateInfo immediateCmdAllocInfo = vkinit::command_buffer_allocate_info(m_ImmediateCommandPool, 1);
    VK_CHECK(vkAllocateCommandBuffers(m_GPUDeviceHandle, &immediateCmdAllocInfo, &m_ImmediateCommandBuffer));

    globalDeletionQueue.PushCallback([=]() { vkDestroyCommandPool(m_GPUDeviceHandle, m_ImmediateCommandPool, nullptr);});
}

void VulkanEngine::InitSyncStructures()
{
    //one fence to wait on the gpu to render
    //two semaphores to sync render with swapchain (get a new image and send a image to present)

    //create it signaled so we can just wait on it on the first frame
    VkFenceCreateInfo fenceCreateInfo = vkinit::fence_create_info(VK_FENCE_CREATE_SIGNALED_BIT);
    VkSemaphoreCreateInfo semaphoreCreateInfo = vkinit::semaphore_create_info();

    for (int i = 0; i < FRAME_MAX; i++)
    {
        VK_CHECK(vkCreateFence(m_GPUDeviceHandle, &fenceCreateInfo, nullptr, &m_Frames[i].renderFence));

        VK_CHECK(vkCreateSemaphore(m_GPUDeviceHandle, &semaphoreCreateInfo, nullptr, &m_Frames[i].swapchainSemaphore));
        VK_CHECK(vkCreateSemaphore(m_GPUDeviceHandle, &semaphoreCreateInfo, nullptr, &m_Frames[i].renderSemaphore));
    }

    VK_CHECK(vkCreateFence(m_GPUDeviceHandle, &fenceCreateInfo, nullptr, &m_ImmediateFence));
    globalDeletionQueue.PushCallback([=]() { vkDestroyFence(m_GPUDeviceHandle, m_ImmediateFence, nullptr); });
}


void VulkanEngine::InitBuffers()
{
    VkBufferCreateInfo bufferInfo = {};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = sizeof(ComputeUniformBufferLayout);
    bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo vmaAllocInfo = {};
    vmaAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    vmaAllocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;

    //revisar como foi feito esse descriptor set a mais com esse buffer a mais
    //adicionar tantos descriptors e buffers quanto frames in flight, pros dados serem updatados pela cpu e serem consumidos pela gpu 
    // (e não serem mais tocados até a gpu termianr de consumir aquele frame, a cpu escreve outro frame e outro buffer)

    for (int i = 0; i < FRAME_MAX; i++)
    {
        VK_CHECK(vmaCreateBuffer(m_Allocator, &bufferInfo, &vmaAllocInfo, &m_Frames[i].ubo.m_UniformBuffer, &m_Frames[i].ubo.m_UniformBufferAlloc, nullptr));
        VK_CHECK(vmaMapMemory(m_Allocator, m_Frames[i].ubo.m_UniformBufferAlloc, &m_Frames[i].ubo.m_UniformBufferCPUMappedMemory));
        memcpy((char*)m_Frames[i].ubo.m_UniformBufferCPUMappedMemory, &m_Frames[i].ubo.m_ComputeUniformBuffer, sizeof(m_Frames[i].ubo.m_ComputeUniformBuffer));
    }

	globalDeletionQueue.PushCallback([&]() {

        for (int i = 0; i < FRAME_MAX; i++)
        {
            vmaUnmapMemory(m_Allocator, m_Frames[i].ubo.m_UniformBufferAlloc);
            vmaDestroyBuffer(m_Allocator, m_Frames[i].ubo.m_UniformBuffer, m_Frames[i].ubo.m_UniformBufferAlloc);
        }
		});
}

void VulkanEngine::InitPipelines()
{
    InitBackgroundPipelines();
}


void VulkanEngine::InitBackgroundPipelines()
{
	VkPushConstantRange pushConstant = {};
	pushConstant.offset = 0;
	pushConstant.size = sizeof(ComputePushConstants);
	pushConstant.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkPipelineLayoutCreateInfo computeLayout = {};
    computeLayout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    computeLayout.pNext = nullptr;
    computeLayout.pSetLayouts = &m_DrawImageDescriptorLayout;
    computeLayout.setLayoutCount = 1;
    computeLayout.pPushConstantRanges = &pushConstant;
    computeLayout.pushConstantRangeCount = 1;

    VK_CHECK(vkCreatePipelineLayout(m_GPUDeviceHandle, &computeLayout, nullptr, &sandboxPipelineLayout));

    VkShaderModule computeDrawShader;

    if (!vkutil::LoadShaderModule("shaders/raytracer.comp.spv", m_GPUDeviceHandle, &computeDrawShader))
    {
        fmt::print("Error when building the compute shader\n");
    }

    VkPipelineShaderStageCreateInfo stageInfo = {};
    stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stageInfo.pNext = nullptr;
    stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stageInfo.module = computeDrawShader;
    stageInfo.pName = "main";
    
    VkComputePipelineCreateInfo computePipelineCreateInfo = {};
    computePipelineCreateInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    computePipelineCreateInfo.pNext = nullptr;
    computePipelineCreateInfo.layout = sandboxPipelineLayout;
    computePipelineCreateInfo.stage = stageInfo;

    VK_CHECK(vkCreateComputePipelines(m_GPUDeviceHandle, VK_NULL_HANDLE, 1, &computePipelineCreateInfo, nullptr, &sandboxPipeline));

    vkDestroyShaderModule(m_GPUDeviceHandle, computeDrawShader, nullptr);

    globalDeletionQueue.PushCallback([&]() {
        vkDestroyPipelineLayout(m_GPUDeviceHandle, sandboxPipelineLayout, nullptr);
        vkDestroyPipeline(m_GPUDeviceHandle, sandboxPipeline, nullptr);
        });

}

void VulkanEngine::InitProfilingTools()
{
    VkQueryPoolCreateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    info.queryType = VK_QUERY_TYPE_TIMESTAMP;
    info.queryCount = 2;
    vkCreateQueryPool(m_GPUDeviceHandle, &info, nullptr, &m_QueryPool);
}

void VulkanEngine::CreateSwapchain(uint32_t width, uint32_t height)
{
    vkb::SwapchainBuilder swapchainBuilder(m_GPU, m_GPUDeviceHandle, m_WindowSurface);

    m_SwapchainImageFormat = VK_FORMAT_B8G8R8A8_UNORM;
    
    VkSurfaceFormatKHR surfaceFormat = {};
    surfaceFormat.format = m_SwapchainImageFormat;
    surfaceFormat.colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;

    vkb::Swapchain vkbSwapchain = swapchainBuilder
        .set_desired_format(surfaceFormat)
        .set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
        .set_desired_extent(width, height)
        .add_image_usage_flags(VK_IMAGE_USAGE_TRANSFER_DST_BIT).build().value();

    m_SwapchainExtent = vkbSwapchain.extent;
    m_Swapchain = vkbSwapchain.swapchain;
    m_SwapchainImages = vkbSwapchain.get_images().value();
    m_SwapchainImageViews = vkbSwapchain.get_image_views().value();
}

void VulkanEngine::DestroySwapchain()
{
    vkDestroySwapchainKHR(m_GPUDeviceHandle, m_Swapchain, nullptr);

    for (int i = 0; i < m_SwapchainImageViews.size(); i++)
        vkDestroyImageView(m_GPUDeviceHandle, m_SwapchainImageViews[i], nullptr);
}

void VulkanEngine::UpdateUIMenus()
{
	ImGui::Begin("PushConstants window");

	ImGui::SliderFloat("FOV", &m_SceneData.cameraData.fov, 20.0f, 90.0f);
	ImGui::SliderFloat("DoF Dist", &m_SceneData.cameraData.dofDist, 0.0f, 90.0f);
	ImGui::SliderFloat("DoF Blur", &m_SceneData.cameraData.dofBlur, 0.0f, 90.0f);


	ImGui::Text("%.f ms", m_LastFrameTime);

	ImGui::End();


	ImGui::Begin("Scene Editor");

	// Sphere list 
	ImGui::SeparatorText("Spheres");

	ComputeUniformBufferLayout& ubo = m_SceneData;

	for (int i = 0; i < ubo.m_SpheresCount; i++)
	{
		Sphere& sphere = ubo.m_Spheres[i];

		char label[32];
		snprintf(label, sizeof(label), "Sphere %d", i);

		if (ImGui::TreeNode(label))
		{
			// position
			float center[3] = { sphere.centerRadius.x, sphere.centerRadius.y, sphere.centerRadius.z };
			if (ImGui::DragFloat3("Position", center, 0.01f, -100.0f, 100.0f))
			{
				sphere.centerRadius.x = center[0];
				sphere.centerRadius.y = center[1];
				sphere.centerRadius.z = center[2];
			}

			// radius
			ImGui::DragFloat("Radius", &sphere.centerRadius.w, 0.01f, 0.01f, 200.0f);

			const char* materialNames[] = { "Lambertian", "Metal", "Dielectric" };
			ImGui::Combo("Material", &sphere.materialType, materialNames, 3);

			ImGui::TreePop();
		}
	}

	ImGui::Spacing();
	if (ubo.m_SpheresCount < 32)
	{
		if (ImGui::Button("Add Sphere"))
		{
			Sphere& newSphere = ubo.m_Spheres[ubo.m_SpheresCount];
			newSphere.centerRadius = glm::vec4(0.0f, 0.0f, 0.0f, 0.5f);
			newSphere.materialType = 0;
			newSphere._p0 = 0; newSphere._p1 = 0; newSphere._p2 = 0;
			ubo.m_SpheresCount++;
		}
	}
	else
	{
		ImGui::TextDisabled("Max spheres reached (32)");
	}

	if (ubo.m_SpheresCount > 0)
	{
		ImGui::SameLine();
		if (ImGui::Button("Remove Last"))
			ubo.m_SpheresCount--;
	}

	// Material editor 
	ImGui::Spacing();
	ImGui::SeparatorText("Materials");

	const char* materialNames[] = { "Lambertian", "Metal", "Dielectric" };

	for (int i = 0; i < 3; i++)
	{
		Material& mat = ubo.m_Materials[i];

		if (ImGui::TreeNode(materialNames[i]))
		{
			float color[3] = { mat.attenuation.r, mat.attenuation.g, mat.attenuation.b };
			if (ImGui::ColorEdit3("Attenuation", color))
			{
				mat.attenuation.r = color[0];
				mat.attenuation.g = color[1];
				mat.attenuation.b = color[2];
			}

			if (i == 1) // metal only
				ImGui::SliderFloat("Roughness", &mat.roughness, 0.0f, 1.0f);

			if (i == 2) // dielectric only
				ImGui::SliderFloat("Refraction Index", &mat.refractionIdx, 1.0f, 3.0f);

			ImGui::TreePop();
		}
	}

	ImGui::End();
}

void VulkanEngine::InitDescriptors()
{
    std::vector<PoolSizeRatio> sizes;
    sizes.push_back({ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1 });
    sizes.push_back({ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 });
    sizes.push_back({ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 });

    m_GlobalDescriptorAllocator.InitPool(m_GPUDeviceHandle, 10, sizes);

    DescriptorSetLayoutBuilder builder;
    builder.AddBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
    builder.AddBinding(1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, FRAME_MAX);
    builder.AddBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);

    m_DrawImageDescriptorLayout = builder.Build(m_GPUDeviceHandle, VK_SHADER_STAGE_COMPUTE_BIT);

    m_DrawImageDescriptors = m_GlobalDescriptorAllocator.Allocate(m_GPUDeviceHandle, m_DrawImageDescriptorLayout);

    VkDescriptorImageInfo imageInfo = {};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    imageInfo.imageView = m_DrawImage.view;

    VkWriteDescriptorSet drawImageWrite = {};
    drawImageWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    drawImageWrite.pNext = nullptr;

    drawImageWrite.dstBinding = 0;
    drawImageWrite.dstSet = m_DrawImageDescriptors;
    drawImageWrite.descriptorCount = 1;
    drawImageWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    drawImageWrite.pImageInfo = &imageInfo;

	VkDescriptorBufferInfo uboInfo[FRAME_MAX] = {};
    for (int i = 0; i < FRAME_MAX; i++)
    {
        uboInfo[i].buffer = m_Frames[i].ubo.m_UniformBuffer;
	    //uboInfo[i].offset = i * sizeof(ComputeUniformBufferLayout);
	    uboInfo[i].offset = 0 * sizeof(ComputeUniformBufferLayout);
	    uboInfo[i].range = sizeof(ComputeUniformBufferLayout);
    }
    

	VkWriteDescriptorSet descriptorWrite = {};
	descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	descriptorWrite.dstSet = m_DrawImageDescriptors;
	descriptorWrite.dstBinding = 1;
	descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	descriptorWrite.descriptorCount = FRAME_MAX;
	descriptorWrite.pBufferInfo = &uboInfo[0];





	VkDescriptorImageInfo imageInfoVectors = {};
	imageInfoVectors.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	imageInfoVectors.imageView = m_UnitRandomVectorTexture.view;
    imageInfoVectors.sampler = m_UnitRandomVectorTexture.sampler;

	VkWriteDescriptorSet drawImageWriteVectors = {};
	drawImageWriteVectors.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	drawImageWriteVectors.pNext = nullptr;

	drawImageWriteVectors.dstBinding = 2;
	drawImageWriteVectors.dstSet = m_DrawImageDescriptors;
	drawImageWriteVectors.descriptorCount = 1;
	drawImageWriteVectors.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	drawImageWriteVectors.pImageInfo = &imageInfoVectors;


    VkWriteDescriptorSet setsArr[3] = { drawImageWrite, descriptorWrite, drawImageWriteVectors };

    vkUpdateDescriptorSets(m_GPUDeviceHandle, 3, &setsArr[0], 0, nullptr);

    globalDeletionQueue.PushCallback([&]()
    {
        m_GlobalDescriptorAllocator.DestroyPool(m_GPUDeviceHandle);
        vkDestroyDescriptorSetLayout(m_GPUDeviceHandle, m_DrawImageDescriptorLayout, nullptr);
    });
}

void VulkanEngine::InitImgui()
{
//  the size of the pool is very oversize, but it's cop'ied from imgui demo
//  itself.
	VkDescriptorPoolSize poolSizes[] = { { VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
		{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
		{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
		{ VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 } };

    VkDescriptorPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = 1000;
    poolInfo.poolSizeCount = (uint32_t)std::size(poolSizes);
    poolInfo.pPoolSizes = poolSizes;

    VkDescriptorPool imguiPool;
    VK_CHECK(vkCreateDescriptorPool(m_GPUDeviceHandle, &poolInfo, nullptr, &imguiPool));

    ImGui::CreateContext();
    ImGui_ImplSDL2_InitForVulkan(m_Window);

    ImGui_ImplVulkan_InitInfo initInfo = {};
    initInfo.Instance = m_Instance;
    initInfo.PhysicalDevice = m_GPU;
    initInfo.Device = m_GPUDeviceHandle;
    initInfo.Queue = m_GraphicsQueue;
    initInfo.DescriptorPool = imguiPool;
    initInfo.MinImageCount = 3;
    initInfo.ImageCount = 3;
    initInfo.UseDynamicRendering = true;

    initInfo.PipelineRenderingCreateInfo = { .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
    initInfo.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
    initInfo.PipelineRenderingCreateInfo.pColorAttachmentFormats = &m_SwapchainImageFormat;

    initInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

    ImGui_ImplVulkan_Init(&initInfo);
    ImGui_ImplVulkan_CreateFontsTexture();

	globalDeletionQueue.PushCallback([=]() {
		ImGui_ImplVulkan_Shutdown();
		vkDestroyDescriptorPool(m_GPUDeviceHandle, imguiPool, nullptr);
		});

}

VkCommandBuffer VulkanEngine::BeginSingleSubmissionCommands()
{
	//Create a new primary (directly to queue) command buffer
	VkCommandBufferAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocInfo.commandPool = m_ImmediateCommandPool;
	allocInfo.commandBufferCount = 1;

	VkCommandBuffer commandBuffer;
	vkAllocateCommandBuffers(m_GPUDeviceHandle, &allocInfo, &commandBuffer);

	VkCommandBufferBeginInfo beginInfo = {};
	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

	//We are only going to use that command buffer once, this is, just to copy memory and exit. So we just give this hint to the driver and let it to optimize if it wants.
	//Also, with this flag we say that we are not going to reset this command buffer explicitly (via Reset command) or implicitly (with BeginCommandBuffer)
	beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

	vkBeginCommandBuffer(commandBuffer, &beginInfo);

	return commandBuffer;
}

void VulkanEngine::EndSingleSubmissionCommands(VkCommandBuffer commandBuffer)
{
	vkEndCommandBuffer(commandBuffer);

	VkSubmitInfo submitInfo = {};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &commandBuffer;

	VkFenceCreateInfo memoryTransferredFenceInfo = {};
	memoryTransferredFenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	VkFence memoryTransferredFence;

	vkCreateFence(m_GPUDeviceHandle, &memoryTransferredFenceInfo, nullptr, &memoryTransferredFence);

	vkQueueSubmit(m_GraphicsQueue, 1, &submitInfo, memoryTransferredFence);

	//We submit our memory transfer command and then we just freeze the CPU waiting to the memory to be transferred
	//we then use this allocated memory to draw in the future. So we will wait all previous commands to reach this point, and then we can continue to work from here. 
	//That way we don't have any race condition and commands from now will be updated.
	//we also could use VkQueueWaitIdle with the graphics queue. The API documentation says:
	/*"vkQueueWaitIdle is equivalent to having submitted a valid fence to every previously executed queue submission command that accepts a fence,
	then waiting for all of those fences to signal using vkWaitForFences with an infinite timeout and waitAll set to VK_TRUE."*/
	//and this is equivalent to use the fence and block the CPU until it has reached that point (where all our command list/copy commands are finished).
	vkWaitForFences(m_GPUDeviceHandle, 1, &memoryTransferredFence, VK_TRUE, UINT64_MAX);

	//vkQueueWaitIdle(m_GraphicsQueueHandle);

	vkDestroyFence(m_GPUDeviceHandle, memoryTransferredFence, nullptr);

	//since this function is only for single time command buffers (send once) we will free it here
	vkFreeCommandBuffers(m_GPUDeviceHandle, m_ImmediateCommandPool, 1, &commandBuffer);
}
