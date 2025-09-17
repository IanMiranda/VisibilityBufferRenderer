#include "Utils.h"

#include <fstream>

namespace im::utils
{
	VkVertexInputAttributeDescription InputAttribute(uint32_t binding, uint32_t location, VkFormat format, uint32_t offset)
	{
		VkVertexInputAttributeDescription res{};
		res.binding = binding;
		res.location = location;
		res.format = format;
		res.offset = offset;
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

		res.second.offset = { 0, 0 };
		res.second.extent = size;
	
		return res;
	}

	std::vector<char> ReadFile(const std::filesystem::path& path)
	{
		std::ifstream file(path, std::ios::binary | std::ios::ate);
		if (!file.is_open())
		{
			std::cerr << "Error: failed to open file with path '" << path << "\'\n";
			return {};
		}

		auto size = file.tellg();
		std::vector<char> res(size);
		file.seekg(0, std::ios::beg);
		file.read(res.data(), size);

		return res;
	}
}
