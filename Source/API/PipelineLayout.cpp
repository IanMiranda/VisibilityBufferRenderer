#include "PipelineLayout.h"

#include "Device.h"
#include "DescriptorSetLayout.h"

namespace im
{
	PipelineLayout::PipelineLayout(
		Device& device,
		const std::vector<VkDescriptorSetLayout>& setLayouts,
		const std::vector<VkPushConstantRange>& pushConstantRanges)
		: mDevice(device)
	{
		VkPipelineLayoutCreateInfo layoutInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
		layoutInfo.setLayoutCount = setLayouts.size();
		layoutInfo.pSetLayouts = setLayouts.data();
		layoutInfo.pushConstantRangeCount = pushConstantRanges.size();
		layoutInfo.pPushConstantRanges = pushConstantRanges.data();
		
		VK_CHECK(vkCreatePipelineLayout(mDevice.Get(), &layoutInfo, nullptr, &mLayout));
	}

	PipelineLayout::~PipelineLayout()
	{
		mDevice.WaitIdle();

		vkDestroyPipelineLayout(mDevice.Get(), mLayout, nullptr);
	}
}