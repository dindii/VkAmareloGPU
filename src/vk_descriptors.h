#pragma once
#include <vk_types.h>


struct DescriptorSetLayoutBuilder
{
	std::vector<VkDescriptorSetLayoutBinding> bindings;
	
	void AddBinding(uint32_t binding, VkDescriptorType type, int count = 1);
	void Clear();

	VkDescriptorSetLayout Build(VkDevice device, VkShaderStageFlags shaderStages, void* pNext = nullptr, VkDescriptorSetLayoutCreateFlags flags = 0);
};

struct PoolSizeRatio
{
	VkDescriptorType type = {};
	float ratio;
};

struct DescriptorAllocator
{
	VkDescriptorPool m_Pool = {};;

	void InitPool(VkDevice device, uint32_t maxSets, std::span<PoolSizeRatio> poolRatios);
	void ClearDescriptors(VkDevice device);
	void DestroyPool(VkDevice device);

	VkDescriptorSet Allocate(VkDevice device, VkDescriptorSetLayout layout);
};