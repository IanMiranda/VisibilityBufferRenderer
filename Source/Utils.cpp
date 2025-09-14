#include "Utils.h"

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
}
