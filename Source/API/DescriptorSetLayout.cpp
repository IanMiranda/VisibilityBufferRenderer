#include "DescriptorSetLayout.h"

#include "Device.h"

namespace im
{
	DescriptorSetLayout::DescriptorSetLayout(Device& device, std::initializer_list<VkDescriptorSetLayoutBinding> bindings)
		: mDevice(device)
	{
		VkDescriptorSetLayoutCreateInfo setLayoutInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
		setLayoutInfo.bindingCount = bindings.size();
		setLayoutInfo.pBindings = bindings.begin();

		VK_CHECK(vkCreateDescriptorSetLayout(mDevice.Get(), &setLayoutInfo, nullptr, &mLayout));
	}

    DescriptorSetLayout::DescriptorSetLayout(
		Device& device,
		std::initializer_list<VkDescriptorSetLayoutBinding> bindings,
		std::initializer_list<VkDescriptorBindingFlags> flags)
		: mDevice(device)
    {
		VkDescriptorSetLayoutBindingFlagsCreateInfo flagsInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO };
		flagsInfo.bindingCount = flags.size();
		flagsInfo.pBindingFlags = flags.begin();
		
		VkDescriptorSetLayoutCreateInfo setLayoutInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
		setLayoutInfo.pNext = &flagsInfo;
		setLayoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT; // Assume UAB
		setLayoutInfo.bindingCount = bindings.size();
		setLayoutInfo.pBindings = bindings.begin();

		VK_CHECK(vkCreateDescriptorSetLayout(mDevice.Get(), &setLayoutInfo, nullptr, &mLayout));
    }

    DescriptorSetLayout::~DescriptorSetLayout()
	{
		mDevice.WaitIdle();
		vkDestroyDescriptorSetLayout(mDevice.Get(), mLayout, nullptr);
	}

	VkDescriptorSetLayoutBinding DescriptorSetLayout::Binding(
		uint32_t index,
		VkDescriptorType type,
		VkShaderStageFlags stages,
		uint32_t count,
		VkSampler* immutableSampler)
	{
		VkDescriptorSetLayoutBinding binding{};
		binding.binding = index;
		binding.descriptorCount = count;
		binding.descriptorType = type;
		binding.stageFlags = stages;
		binding.pImmutableSamplers = immutableSampler;
		return binding;
	}
}