#pragma once

#include "Common.h"

namespace im
{
	class Device;

	class DescriptorSetLayout
	{
	public:
		DescriptorSetLayout(Device& device);
		~DescriptorSetLayout();

		DescriptorSetLayout& AddBinding(uint32_t index, VkDescriptorType type, VkShaderStageFlags stages, uint32_t count = 1);
		
		void Commit();

		VkDescriptorSetLayout Get() const { return mLayout; }

	private:
		Device& mDevice;

		VkDescriptorSetLayout mLayout{ VK_NULL_HANDLE };
		std::vector<VkDescriptorSetLayoutBinding> mBindings;
	};
}