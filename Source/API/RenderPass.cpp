#include "RenderPass.h"

namespace im
{
	VkRect2D Scissor(VkExtent2D size)
	{
		VkRect2D res{};
		res.offset = { 0, 0 };
		res.extent = size;
		return res;
	}

	std::pair<VkViewport, VkRect2D> ViewportAndScissor(VkExtent2D size)
	{
		// Assume negative viewport height
		std::pair<VkViewport, VkRect2D> res;

		res.first.x = 0.0f;
		res.first.y = static_cast<float>(size.height);
		res.first.width = size.width;
		res.first.height = -static_cast<float>(size.height);
		res.first.minDepth = 0.0f;
		res.first.maxDepth = 1.0f;
		res.second = Scissor(size);

		return res;
	}

	VkClearValue ClearColor(const glm::vec4& value)
	{
		VkClearValue res{};
		res.color = { value.r, value.g, value.b, value.a };
		return res;
	}

	VkClearValue ClearDepth(float depth, uint32_t stencil)
	{
		VkClearValue res{};
		res.depthStencil.depth = depth;
		res.depthStencil.stencil = stencil;
		return res;
	}

	VkRenderingAttachmentInfo ColorAttachment(VkImageView view, VkAttachmentLoadOp load, VkAttachmentStoreOp store, VkClearValue clear)
	{
		VkRenderingAttachmentInfo colorAttach{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
		colorAttach.clearValue = clear;
		colorAttach.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		colorAttach.imageView = view;
		colorAttach.loadOp = load;
		colorAttach.storeOp = store;
		return colorAttach;
	}

	VkRenderingAttachmentInfo DepthAttachment(VkImageView view, VkAttachmentLoadOp load, VkAttachmentStoreOp store, VkClearValue clear)
	{
		VkRenderingAttachmentInfo depthAttach{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
		depthAttach.clearValue = clear;
		depthAttach.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		depthAttach.imageView = view;
		depthAttach.loadOp = load;
		depthAttach.storeOp = store;
		return depthAttach;
	}
}