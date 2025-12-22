#pragma once

#include <span>

#include "Common.h"

namespace im
{
	class Device;
	class Shader;
	class PipelineLayout;

	struct InputAttribute
	{
		uint32_t location;
		VkFormat format;
		uint32_t offset;

		InputAttribute(uint32_t location, VkFormat format, uint32_t offset)
			: location(location), format(format), offset(offset)
		{}
	};

	struct InputBinding
	{
		std::vector<InputAttribute> attributes;
		VkVertexInputRate inputRate;
		uint32_t stride;

		InputBinding(
			const std::vector<InputAttribute> attributes,
			VkVertexInputRate inputRate,
			uint32_t stride)
			: attributes(attributes)
			, inputRate(inputRate)
			, stride(stride)
		{}
	};

	VkPipelineInputAssemblyStateCreateInfo InputAssembly(
		VkPrimitiveTopology topology,
		bool enableRestart = false);

	VkPipelineRasterizationStateCreateInfo Rasterizer(
		VkCullModeFlags cull,
		VkFrontFace frontFace,
		VkPolygonMode polygonMode);

	VkPipelineMultisampleStateCreateInfo Multisample(
		VkSampleCountFlagBits samples
	);

	using ColorBlendAttachment = std::pair<VkFormat, VkPipelineColorBlendAttachmentState>;
	ColorBlendAttachment ColorAttachment(VkFormat format);

	using DepthStencilAttachment = std::pair<VkFormat, VkPipelineDepthStencilStateCreateInfo>;
	DepthStencilAttachment DepthStencil(VkFormat format, bool depthWrite = true);

	class GraphicsPipelineDesc
	{
		friend class GraphicsPipeline;
	public:
		GraphicsPipelineDesc(
			const PipelineLayout& layout,
			const Shader& shader,
			const std::vector<InputBinding>& bindings,
			const VkPipelineInputAssemblyStateCreateInfo& inputAssembly,
			const VkPipelineRasterizationStateCreateInfo& rasterizer,
			const VkPipelineMultisampleStateCreateInfo& multisample,
			const std::vector<ColorBlendAttachment>& colorAttachments,
			std::optional<std::pair<VkFormat, VkPipelineDepthStencilStateCreateInfo>> depthStencil = std::nullopt
		);

	private:
		std::vector<VkVertexInputBindingDescription> GetVertexInputBindings(const std::vector<InputBinding>& bindings);
		std::vector<VkVertexInputAttributeDescription> GetVertexInputAttribs(const std::vector<InputBinding>& bindings);
	
	private:
		const PipelineLayout& mLayout;
		const Shader& mShader;

		std::vector<VkVertexInputBindingDescription> mInputBindings;
		std::vector<VkVertexInputAttributeDescription> mInputAttribs;

		VkPipelineVertexInputStateCreateInfo mVertexInput{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
		VkPipelineInputAssemblyStateCreateInfo mInputAssembly;
		VkPipelineRasterizationStateCreateInfo mRasterizer;
		VkPipelineMultisampleStateCreateInfo mMultisample;
		std::vector<VkFormat> mColorFormats;
		std::vector<VkPipelineColorBlendAttachmentState> mColorBlendStates;
		std::optional<DepthStencilAttachment> mDepthStencil;
	};

	class GraphicsPipeline
	{
	public:
		GraphicsPipeline(Device& device, const GraphicsPipelineDesc& desc);
		~GraphicsPipeline();

		GraphicsPipeline(const GraphicsPipeline& other) = delete;
		GraphicsPipeline& operator=(const GraphicsPipeline& other) = delete;

		VkPipeline Get() const { return mPipeline; }
	
	private:
		Device& mDevice;

		VkPipeline mPipeline;
	};
}