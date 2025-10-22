#pragma once

#include "Common.h"

namespace im
{
	class Device;
	class Shader;
	class PipelineLayout;

	class GraphicsPipeline
	{
	public:
		GraphicsPipeline(Device& device, PipelineLayout& layout, Shader& shader);
		~GraphicsPipeline();

		GraphicsPipeline& SetVertexInput(
			const std::vector<VkVertexInputBindingDescription>& bindings,
			const std::vector<VkVertexInputAttributeDescription>& attributes);
		GraphicsPipeline& SetPrimitiveTopology(VkPrimitiveTopology topology);
		GraphicsPipeline& SetRasterizer(VkCullModeFlags cull, VkFrontFace frontFace, VkPolygonMode polygonMode);
		GraphicsPipeline& SetMsaaSamples(VkSampleCountFlagBits samples);
		GraphicsPipeline& AddColorAttachment(VkFormat format);
		GraphicsPipeline& SetDepthAttachment(VkFormat format, bool depthWrite);

		void Commit();

		VkPipeline Get() const { return mPipeline; }
	
	private:
		Device& mDevice;
		PipelineLayout& mLayout;
		Shader& mShader;

		VkPipeline mPipeline;

		VkPipelineVertexInputStateCreateInfo mVertexInput{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
		VkPipelineInputAssemblyStateCreateInfo mInputAssembly{ VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
		VkPipelineRasterizationStateCreateInfo mRasterizer{ VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
		VkPipelineMultisampleStateCreateInfo mMultisample{ VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
		VkPipelineDepthStencilStateCreateInfo mDepthStencil{ VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };

		std::vector<VkVertexInputBindingDescription> mInputBindings;
		std::vector<VkVertexInputAttributeDescription> mInputAttribs;
		std::vector<VkPipelineColorBlendAttachmentState> mColorBlendStates;
		std::vector<VkFormat> mColorAttachmentFormats;
		VkFormat mDepthFormat{ VK_FORMAT_UNDEFINED };
	};
}