#pragma once

#include <filesystem>

#include "Common.h"

namespace im::utils
{
	VkVertexInputBindingDescription InputBinding(uint32_t binding, VkVertexInputRate rate, uint32_t stride);
	VkVertexInputAttributeDescription InputAttribute(uint32_t binding, uint32_t location, VkFormat format, uint32_t offset);

	std::vector<char> ReadFile(const std::filesystem::path& path);

	std::pair<std::vector<Vertex>, std::vector<uint32_t>> LoadGltfModel(const std::filesystem::path& path);
}