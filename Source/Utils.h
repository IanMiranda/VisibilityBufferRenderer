#pragma once

#include "Common.h"

namespace im::utils
{
	VkVertexInputAttributeDescription InputAttribute(uint32_t binding, uint32_t location, VkFormat format, uint32_t offset);
}