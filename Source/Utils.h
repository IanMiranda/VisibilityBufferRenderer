#pragma once

#include <filesystem>

#include "Common.h"

namespace im::utils
{
	VkVertexInputAttributeDescription InputAttribute(uint32_t binding, uint32_t location, VkFormat format, uint32_t offset);
	std::pair<VkViewport, VkRect2D> ViewportAndScissor(VkExtent2D size);

	std::vector<char> ReadFile(const std::filesystem::path& path);
}