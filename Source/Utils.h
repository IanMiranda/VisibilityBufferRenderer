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
	
	VkClearValue ClearColor(const glm::vec4& value = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
	VkClearValue ClearDepth(float depth = 1.0f, uint32_t stencil = 0);

	VkRenderingAttachmentInfo ColorAttachment(VkImageView view, VkAttachmentLoadOp load, VkAttachmentStoreOp store, VkClearValue clear = ClearColor());
	VkRenderingAttachmentInfo DepthAttachment(VkImageView view, VkAttachmentLoadOp load, VkAttachmentStoreOp store, VkClearValue clear = ClearDepth());

	VkPushConstantRange PushConstantRange(VkShaderStageFlags stages, uint32_t size, uint32_t offset = 0);
}