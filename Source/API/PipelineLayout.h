#pragma once

#include "Common.h"

namespace im
{
	class Device;
	class DescriptorSetLayout;

	VkPushConstantRange PushConstantRange(VkShaderStageFlags stages, uint32_t size, uint32_t offset = 0);

	class PipelineLayout
	{
	public:
		PipelineLayout(
			Device& device,
			std::initializer_list<std::reference_wrapper<DescriptorSetLayout>> setLayouts,
			std::initializer_list<VkPushConstantRange> pushConstantRanges);
		~PipelineLayout();

		PipelineLayout(const PipelineLayout& other) = delete;
		PipelineLayout& operator=(const PipelineLayout& other) = delete;

		VkPipelineLayout Get() const { return mLayout; }

	private:
		Device& mDevice;

		VkPipelineLayout mLayout;
	};
}