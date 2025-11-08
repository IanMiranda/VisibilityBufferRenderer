#include "Common.h"

#include "Utils.h"

namespace im
{
	std::vector<VkVertexInputAttributeDescription> Vertex::GetInputAttributes()
	{
		return {
			utils::InputAttribute(0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0),
			utils::InputAttribute(0, 1, VK_FORMAT_R32G32B32A32_SFLOAT, sizeof(float) * 3),
			utils::InputAttribute(0, 2, VK_FORMAT_R32G32_SFLOAT, sizeof(float) * 7),
			utils::InputAttribute(0, 3, VK_FORMAT_R32G32B32_SFLOAT, sizeof(float) * 9),
			utils::InputAttribute(0, 4, VK_FORMAT_R32G32B32_SFLOAT, sizeof(float) * 12),
			utils::InputAttribute(0, 5, VK_FORMAT_R32G32B32_SFLOAT, sizeof(float) * 15)
		};
	}
}