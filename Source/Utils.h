#pragma once

#include <filesystem>

#include "Common.h"

namespace im::utils
{
	VkVertexInputAttributeDescription InputAttribute(uint32_t binding, uint32_t location, VkFormat format, uint32_t offset);
	std::pair<VkViewport, VkRect2D> ViewportAndScissor(VkExtent2D size);

	std::vector<char> ReadFile(const std::filesystem::path& path);

	std::pair<std::vector<Vertex>, std::vector<uint32_t>> LoadModel(const std::filesystem::path& path);

	VkPipelineDynamicStateCreateInfo PipelineDynamicState(std::vector<VkDynamicState>& dynamicStates);

	VkPipelineVertexInputStateCreateInfo PipelineVertexInput(
		std::vector<VkVertexInputBindingDescription>& bindings,
		std::vector<VkVertexInputAttributeDescription>& attributes);

	VkPipelineInputAssemblyStateCreateInfo PipelineInputAssembly(VkPrimitiveTopology primitive);

	VkPipelineViewportStateCreateInfo PipelineViewport();

	VkRenderingAttachmentInfo RenderingDepthAttachment(VkImageView view, VkAttachmentLoadOp load, VkAttachmentStoreOp store);
}