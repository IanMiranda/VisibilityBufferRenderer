#include "PipelineLayout.h"

#include "Device.h"
#include "DescriptorSetLayout.h"

namespace im
{
	PipelineLayout::PipelineLayout(
		Device& device,
		const std::vector<std::reference_wrapper<DescriptorSetLayout>>& setLayouts,
		const std::vector<VkPushConstantRange>& pushConstantRanges)
		: mDevice(device)
	{
		std::vector<VkDescriptorSetLayout> vulkanSetLayouts;
		vulkanSetLayouts.reserve(setLayouts.size());
		for (const auto& setLayout : setLayouts)
			vulkanSetLayouts.emplace_back(setLayout.get().Get());
		
		VkPipelineLayoutCreateInfo layoutInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
		layoutInfo.setLayoutCount = vulkanSetLayouts.size();
		layoutInfo.pSetLayouts = vulkanSetLayouts.data();
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