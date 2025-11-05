#pragma once

#include "Common.h"

namespace im
{
	class Device;

	class DescriptorSetLayout
	{
	public:
		DescriptorSetLayout(Device& device, const std::vector<VkDescriptorSetLayoutBinding>& bindings);
		DescriptorSetLayout(
			Device& device,
			const std::vector<VkDescriptorSetLayoutBinding>& bindings,
			const std::vector<VkDescriptorBindingFlags>& flags
		);
		~DescriptorSetLayout();

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