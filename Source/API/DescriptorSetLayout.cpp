#include "DescriptorSetLayout.h"

#include "Device.h"

namespace im
{
	DescriptorSetLayout::DescriptorSetLayout(Device& device)
		: mDevice(device)
	{
	}

	DescriptorSetLayout::~DescriptorSetLayout()
	{
		mDevice.WaitIdle();
		vkDestroyDescriptorSetLayout(mDevice.Get(), mLayout, nullptr);
	}

	DescriptorSetLayout& DescriptorSetLayout::AddBinding(
		uint32_t index,
		VkDescriptorType type,
		VkShaderStageFlags stages,
		uint32_t count)
	{
		VkDescriptorSetLayoutBinding binding{};
		binding.binding = index;
		binding.descriptorCount = count;
		binding.descriptorType = type;
		binding.stageFlags = stages;
		mBindings.emplace_back(binding);

		return *this;
	}

	void DescriptorSetLayout::Commit()
	{
		VkDescriptorSetLayoutCreateInfo setLayoutInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
		setLayoutInfo.bindingCount = mBindings.size();
		setLayoutInfo.pBindings = mBindings.data();

		VK_CHECK(vkCreateDescriptorSetLayout(mDevice.Get(), &setLayoutInfo, nullptr, &mLayout));

		mBindings.clear();
	}

}