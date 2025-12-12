#pragma once

#include "Common.h"

namespace im
{
	class Device;

	class DescriptorSetLayout
	{
	public:
		DescriptorSetLayout(Device& device, std::initializer_list<VkDescriptorSetLayoutBinding> bindings);
		DescriptorSetLayout(
			Device& device,
			std::initializer_list<VkDescriptorSetLayoutBinding> bindings,
			std::initializer_list<VkDescriptorBindingFlags> flags
		);
		~DescriptorSetLayout();

		DescriptorSetLayout(const DescriptorSetLayout& other) = delete;
		DescriptorSetLayout& operator=(const DescriptorSetLayout& other) = delete;

		static VkDescriptorSetLayoutBinding Binding(
			uint32_t index,
			VkDescriptorType type,
			VkShaderStageFlags stages,
			uint32_t count = 1);
		
		VkDescriptorSetLayout Get() const { return mLayout; }

	private:
		Device& mDevice;

		VkDescriptorSetLayout mLayout{ VK_NULL_HANDLE };
	};
}