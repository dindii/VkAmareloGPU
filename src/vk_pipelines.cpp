#include <vk_pipelines.h>

#include <fstream>
#include <vk_initializers.h>


bool vkutil::LoadShaderModule(const char* filePath, VkDevice device, VkShaderModule* outShaderModule)
{
	std::ifstream file(filePath, std::ios::ate | std::ios::binary);

	if (!file.is_open())
		return false;

	size_t fileSize = (size_t)file.tellg();

	std::vector<uint32_t> buffer(fileSize / sizeof(uint32_t));

	file.seekg(0);
	file.read((char*)buffer.data(), fileSize);
	file.close();


	VkShaderModuleCreateInfo createInfo = {};
	createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	createInfo.pNext = nullptr;

	//size in bytes
	createInfo.codeSize = buffer.size() * sizeof(uint32_t);
	createInfo.pCode = buffer.data();

	VkShaderModule shaderMod = {};

	if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderMod) != VK_SUCCESS)
		return false;

	*outShaderModule = shaderMod;

	return true;

}
