#include "Utils.h"

#include <fstream>
#include <algorithm>
#include <ranges>
#include <tiny_gltf.h>

#include "API/Buffer.h"

namespace im::utils
{
	VkVertexInputBindingDescription InputBinding(uint32_t binding, VkVertexInputRate rate, uint32_t stride)
	{
		VkVertexInputBindingDescription res{};
		res.binding = binding;
		res.inputRate = rate;
		res.stride = stride;
		return res;
	}

	VkVertexInputAttributeDescription InputAttribute(uint32_t binding, uint32_t location, VkFormat format, uint32_t offset)
	{
		VkVertexInputAttributeDescription res{};
		res.binding = binding;
		res.location = location;
		res.format = format;
		res.offset = offset;
		return res;
	}

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

	std::vector<char> ReadFile(const std::filesystem::path& path)
	{
		std::ifstream file(path, std::ios::binary | std::ios::ate);
		if (!file.is_open())
		{
			fmt::println(stderr, "Error: failed to open file with path '{}'!", path);
			return {};
		}

		auto size = file.tellg();
		std::vector<char> res(size);
		file.seekg(0, std::ios::beg);
		file.read(res.data(), size);

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

	VkPushConstantRange PushConstantRange(VkShaderStageFlags stages, uint32_t size, uint32_t offset)
	{
		VkPushConstantRange pushRange{};
		pushRange.stageFlags = stages;
		pushRange.size = size;
		pushRange.offset = offset;
		return pushRange;
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
}
