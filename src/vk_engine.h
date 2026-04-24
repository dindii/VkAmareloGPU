// VkAmareloGPU.h : Include file for standard system include files,
// or project specific include files.

#pragma once

#include <vk_types.h>
#include <functional>
#include "vk_descriptors.h"
#include "vk_mem_alloc.h"
#include <vector>

constexpr unsigned int FRAME_MAX = 2;

struct SDL_Keysym;
struct SDL_MouseMotionEvent;

struct DeletionQueue
{
	std::deque<std::function<void()>> deletors;

	inline void PushCallback(std::function<void()>&& function)	{ deletors.push_back(function); }
	
	void Flush()
	{
		for (auto it = deletors.rbegin(); it != deletors.rend(); it++)
		{
			(*it)();
		}

		deletors.clear();
	}
};

struct Camera
{
	glm::vec4 targetDir = { 0.0890387893, 	0.389418364, 0.916747212, 0.0f };
	glm::vec4 right = { 0.995316505, 0.0f, 0.0966698080, 0.0f };
	glm::vec4 up = { 0.0376449972, 0.921060979, 0.387594521, 1.0f };
	glm::vec4 position = { -0.299381405, 0.806913078, 1.00485790, 1.0f };
	glm::vec4 yawPitch = { 6.38000631, 0.400000036, 0.0f, 0.0f };

	float fov = 90.0f;
	float dofDist = 1.875f;
	float dofBlur = 1.563f;
	float _pad;
};

struct Sphere
{
	glm::vec4 centerRadius;
	int materialType;
	float _p0;
	float _p1;
	float _p2;
};

struct Material
{
	glm::vec4 attenuation;
	float roughness;
	float refractionIdx;
	int type;
	float _pad;
};

struct ComputeUniformBufferLayout
{
	Camera cameraData;
	Sphere m_Spheres[32];
	Material m_Materials[3];
	int m_SpheresCount;
	int _pad0;
	int _pad1;
	int _pad2;
};

struct UniformBuffer
{
	ComputeUniformBufferLayout m_ComputeUniformBuffer;
	VkBuffer m_UniformBuffer = {};
	VmaAllocation m_UniformBufferAlloc = {};
	void* m_UniformBufferCPUMappedMemory = nullptr;
};

struct FrameData
{
	VkCommandPool commandPool = {};
	VkCommandBuffer commandBuffer = {};

	VkSemaphore swapchainSemaphore = {};
	VkSemaphore renderSemaphore = {};
	VkFence renderFence = {};

	UniformBuffer ubo;

	DeletionQueue deletionQueue;
};

struct ComputePushConstants
{
	glm::vec4 data1 = glm::vec4(0.0f, 0.0f, 1.0f, 0.5f);
	glm::vec4 data2 = glm::vec4(0.0f, -100.5f, -1.0f, 100.0f);
	glm::vec4 data3 = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
	glm::vec4 data4;
	int currentFrame = 0;
};

class VulkanEngine {
public:
	static VulkanEngine& Get();

	//initializes everything in the engine
	void Init();

	//shuts down the engine
	void Cleanup();

	void UpdateUBO();

	//draw loop
	void Draw();
	void DrawBackground(VkCommandBuffer cmd);

	void UpdateCamera();
	void AddCameraTargetPosition(glm::vec4 pos);

	//run main loop
	void Run();

	void ProcessInput(const SDL_Keysym& input);
	void ProcessMouse(const SDL_MouseMotionEvent& input);

	AllocatedImage CreateTexture(const ImageProperties& properties, uint8_t* data);
	glm::vec3 CreateRandomVectors();
public:
	glm::vec2 m_LastMouse;

	bool m_MouseLocked = true;
	bool m_bIsInitialized = false;
	int m_FrameCount = 0;
	uint8_t m_CurrentFrame = 0;
	bool m_bStopRendering = false;
	VkExtent2D m_WindowExtent = { 2048 , 1024};

	struct SDL_Window* m_Window = nullptr;

	VkInstance m_Instance = {};
	VkPhysicalDevice m_GPU = {};
	VkDevice m_GPUDeviceHandle = {};
	
	VkSurfaceKHR m_WindowSurface = {};

	VkDebugUtilsMessengerEXT m_DebugMessenger = {};

	bool bUseValidationLayers = true;

	VkSwapchainKHR m_Swapchain = {};
	VkFormat m_SwapchainImageFormat = {};

	std::vector<VkImage> m_SwapchainImages;
	std::vector<VkImageView> m_SwapchainImageViews;
	VkExtent2D m_SwapchainExtent = {};

	ComputeUniformBufferLayout m_SceneData;

	FrameData m_Frames[FRAME_MAX];
	FrameData& GetCurrentFrameContext() { return m_Frames[m_FrameCount % FRAME_MAX]; }
	VkQueue m_GraphicsQueue;
	uint32_t m_GraphicsQueueType;

	VmaAllocator m_Allocator;

	DeletionQueue globalDeletionQueue;

	AllocatedImage m_DrawImage;
	VkExtent2D m_DrawExtent;

	DescriptorAllocator m_GlobalDescriptorAllocator;
	VkDescriptorSet m_DrawImageDescriptors;
	VkDescriptorSetLayout m_DrawImageDescriptorLayout;

	VkPipeline sandboxPipeline;
	VkPipelineLayout sandboxPipelineLayout;


	//Those are just for immediate commands, not to sync with swap chain or other rendering logic.
	VkFence m_ImmediateFence;
	VkCommandBuffer m_ImmediateCommandBuffer;
	VkCommandPool m_ImmediateCommandPool;

	VkQueryPool m_QueryPool;
	double m_TimestampPeriod = 0;
	double m_LastFrameTime = 0;

	void ImmediateSubmit(std::function<void(VkCommandBuffer cmd)>&& function);
	void DrawImGui(VkCommandBuffer cmd, VkImageView targetImageView);

	AllocatedImage m_UnitRandomVectorTexture;

private:
	//init funcs
	void InitVulkan();
	void InitSwapchain();
	void InitCommands();
	void InitSyncStructures();
	void InitBuffers();
	void InitScene();
	void InitPipelines();
	void InitBackgroundPipelines();
	void InitProfilingTools();
	void CreateSwapchain(uint32_t width, uint32_t height);
	void DestroySwapchain();

	void UpdateUIMenus();

	void InitDescriptors();

	void InitImgui();

	VkCommandBuffer BeginSingleSubmissionCommands();
	void EndSingleSubmissionCommands(VkCommandBuffer commandBuffer);
};
