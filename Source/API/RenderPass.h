#pragma once

#include "Common.h"

namespace im
{
	VkRect2D Scissor(VkExtent2D size);
	std::pair<VkViewport, VkRect2D> ViewportAndScissor(VkExtent2D size);


	VkClearValue ClearColor(const glm::vec4& value = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
	VkClearValue ClearDepth(float depth = 1.0f, uint32_t stencil = 0);

	VkRenderingAttachmentInfo ColorAttachment(VkImageView view, VkAttachmentLoadOp load, VkAttachmentStoreOp store, VkClearValue clear = ClearColor());
	VkRenderingAttachmentInfo DepthAttachment(VkImageView view, VkAttachmentLoadOp load, VkAttachmentStoreOp store, VkClearValue clear = ClearDepth());
}