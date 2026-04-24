#include <vk_descriptors.h>

void DescriptorSetLayoutBuilder::AddBinding(uint32_t binding, VkDescriptorType type, int count)
{
	VkDescriptorSetLayoutBinding newBind = {};
	newBind.binding = binding;
	newBind.descriptorCount = count;
	newBind.descriptorType = type;

	bindings.push_back(newBind);
}

void DescriptorSetLayoutBuilder::Clear()
{
	bindings.clear();
}

VkDescriptorSetLayout DescriptorSetLayoutBuilder::Build(VkDevice device, VkShaderStageFlags shaderStages, void* pNext /*= nullptr*/, VkDescriptorSetLayoutCreateFlags flags /* = 0*/)
{
	for (int i = 0; i < bindings.size(); i++)
		bindings[i].stageFlags |= shaderStages;

	VkDescriptorSetLayoutCreateInfo info = {};
	info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	info.pNext = pNext;

	info.pBindings = bindings.data();
	info.bindingCount = (uint32_t)bindings.size();
	info.flags = flags;

	VkDescriptorSetLayout set;
	VK_CHECK(vkCreateDescriptorSetLayout(device, &info, nullptr, &set));

	return set;
}

void DescriptorAllocator::InitPool(VkDevice device, uint32_t maxSets, std::span<PoolSizeRatio> poolRatios)
{
	std::vector<VkDescriptorPoolSize> poolSizes;

	for (int i = 0; i < poolRatios.size(); i++)
	{
		VkDescriptorPoolSize descPoolSize = {};
		descPoolSize.type = poolRatios[i].type;
		descPoolSize.descriptorCount = uint32_t(poolRatios[i].ratio * maxSets);

		poolSizes.push_back(descPoolSize);
	}

	VkDescriptorPoolCreateInfo poolInfo = {};
	poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolInfo.flags = 0;
	poolInfo.maxSets = maxSets;
	poolInfo.poolSizeCount = (uint32_t)poolSizes.size();
	poolInfo.pPoolSizes = poolSizes.data();

	vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_Pool);
}

void DescriptorAllocator::ClearDescriptors(VkDevice device)
{
	vkResetDescriptorPool(device, m_Pool, 0);
}

void DescriptorAllocator::DestroyPool(VkDevice device)
{
	vkDestroyDescriptorPool(device, m_Pool, nullptr);
}

VkDescriptorSet DescriptorAllocator::Allocate(VkDevice device, VkDescriptorSetLayout layout)
{
	VkDescriptorSetAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocInfo.pNext = nullptr;
	allocInfo.descriptorPool = m_Pool;
	allocInfo.descriptorSetCount = 1;
	allocInfo.pSetLayouts = &layout;

	VkDescriptorSet descriptorSet;
	VK_CHECK(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSet));

	return descriptorSet;
}