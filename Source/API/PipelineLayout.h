#pragma once

#include "Common.h"

namespace im
{
	class Device;
	class DescriptorSetLayout;

	class PipelineLayout
	{
	public:
		PipelineLayout(
			Device& device,
			const std::vector<std::reference_wrapper<DescriptorSetLayout>>& setLayouts,
			const std::vector<VkPushConstantRange>& pushConstantRanges);
		~PipelineLayout();

		VkPipelineLayout Get() const { return mLayout; }

	private:
		Device& mDevice;

		VkPipelineLayout mLayout;
	};
}