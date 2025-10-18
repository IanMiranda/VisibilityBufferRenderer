#pragma once

#include <filesystem>

#include "Common.h"

namespace im::utils
{
	VkVertexInputBindingDescription InputBinding(uint32_t binding, VkVertexInputRate rate, uint32_t stride);
	VkVertexInputAttributeDescription InputAttribute(uint32_t binding, uint32_t location, VkFormat format, uint32_t offset);
	VkRect2D Scissor(VkExtent2D size);
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

	VkShaderModule CreateShader(VkDevice device, const std::vector<char>& source);

	VkClearValue ClearColor(const glm::vec4& value = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
	VkClearValue ClearDepth(float depth = 1.0f, uint32_t stencil = 0);
}