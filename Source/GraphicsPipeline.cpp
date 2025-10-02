#include "GraphicsPipeline.h"

#include "Device.h"
#include "PipelineLayout.h"

namespace im
{
	GraphicsPipeline::GraphicsPipeline(Device& device, PipelineLayout& layout)
		: mDevice(device), mLayout(layout)
	{
	}

	GraphicsPipeline::~GraphicsPipeline()
	{
		mDevice.WaitIdle();
		vkDestroyPipeline(mDevice.Get(), mPipeline, nullptr);
	}

	GraphicsPipeline& GraphicsPipeline::AddShader(VkShaderModule shader, VkShaderStageFlagBits stage, const char* entrypoint)
	{
		VkPipelineShaderStageCreateInfo stageInfo{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
		stageInfo.module = shader;
		stageInfo.pName = entrypoint;
		stageInfo.stage = stage;
		mStages.emplace_back(stageInfo);
		return *this;
	}

	GraphicsPipeline& GraphicsPipeline::SetVertexInput(
		const std::vector<VkVertexInputBindingDescription>& bindings,
		const std::vector<VkVertexInputAttributeDescription>& attributes)
	{
		mInputBindings = bindings;
		mInputAttribs = attributes;
		
		mVertexInput.vertexBindingDescriptionCount = mInputBindings.size();
		mVertexInput.pVertexBindingDescriptions = mInputBindings.data();
		mVertexInput.vertexAttributeDescriptionCount = mInputAttribs.size();
		mVertexInput.pVertexAttributeDescriptions = mInputAttribs.data();

		return *this;
	}

	GraphicsPipeline& GraphicsPipeline::SetPrimitiveTopology(VkPrimitiveTopology topology)
	{
		mInputAssembly.topology = topology;

		return *this;
	}

	GraphicsPipeline& GraphicsPipeline::SetRasterizer(VkCullModeFlags cull, VkFrontFace frontFace, VkPolygonMode polygonMode)
	{
		mRasterizer.cullMode = cull;
		mRasterizer.frontFace = frontFace;
		mRasterizer.polygonMode = polygonMode;
		mRasterizer.lineWidth = 1.0f;

		return *this;
	}

	GraphicsPipeline& GraphicsPipeline::SetMsaaSamples(VkSampleCountFlagBits samples)
	{
		mMultisample.rasterizationSamples = samples;

		return *this;
	}

	GraphicsPipeline& GraphicsPipeline::AddColorAttachment(VkFormat format)
	{
		VkPipelineColorBlendAttachmentState colorAttachment{};
		colorAttachment.blendEnable = VK_FALSE;
		colorAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT
			| VK_COLOR_COMPONENT_G_BIT
			| VK_COLOR_COMPONENT_B_BIT
			| VK_COLOR_COMPONENT_A_BIT;
		mColorBlendStates.emplace_back(colorAttachment);
		mColorAttachmentFormats.emplace_back(format);

		return *this;
	}

	GraphicsPipeline& GraphicsPipeline::SetDepthAttachment(VkFormat format, bool depthWrite)
	{
		mDepthFormat = format;

		mDepthStencil.depthTestEnable = true;
		mDepthStencil.depthWriteEnable = depthWrite;
		mDepthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

		return *this;
	}

	void GraphicsPipeline::Commit()
	{
		VkPipelineViewportStateCreateInfo viewport{ VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
		viewport.viewportCount = 1;
		viewport.scissorCount = 1;

		VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
		
		VkPipelineDynamicStateCreateInfo dynamicState{ VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
		dynamicState.dynamicStateCount = std::size(dynamicStates);
		dynamicState.pDynamicStates = dynamicStates;

		VkPipelineRenderingCreateInfo renderingInfo{ VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
		renderingInfo.colorAttachmentCount = mColorAttachmentFormats.size();
		renderingInfo.pColorAttachmentFormats = mColorAttachmentFormats.data();
		renderingInfo.depthAttachmentFormat = mDepthFormat;

		VkPipelineColorBlendStateCreateInfo colorBlend{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
		colorBlend.attachmentCount = mColorBlendStates.size();
		colorBlend.pAttachments = mColorBlendStates.data();
		colorBlend.logicOpEnable = VK_FALSE;

		VkGraphicsPipelineCreateInfo pipelineInfo{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
		pipelineInfo.pNext = &renderingInfo;
		pipelineInfo.stageCount = mStages.size();
		pipelineInfo.pStages = mStages.data();
		pipelineInfo.pVertexInputState = &mVertexInput;
		pipelineInfo.pInputAssemblyState = &mInputAssembly;
		pipelineInfo.pViewportState = &viewport;
		pipelineInfo.pRasterizationState = &mRasterizer;
		pipelineInfo.pMultisampleState = &mMultisample;
		pipelineInfo.pColorBlendState = &colorBlend;
		pipelineInfo.pDynamicState = &dynamicState;
		if (mDepthFormat != VK_FORMAT_UNDEFINED)
			pipelineInfo.pDepthStencilState = &mDepthStencil;
		pipelineInfo.layout = mLayout.Get();

		VK_CHECK(vkCreateGraphicsPipelines(mDevice.Get(), mDevice.GetPipelineCache(), 1, &pipelineInfo, nullptr, &mPipeline));

		mStages.clear();
		mInputBindings.clear();
		mInputAttribs.clear();
		mColorBlendStates.clear();
		mColorAttachmentFormats.clear();
	}
}