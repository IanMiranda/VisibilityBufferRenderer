#include "Common.h"

#include "Utils.h"

namespace im
{
	std::array<VkVertexInputAttributeDescription, 6> Vertex::GetInputAttributes()
	{
		std::array<VkVertexInputAttributeDescription, 6> inputAttribs;
		inputAttribs[0] = utils::InputAttribute(0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0);
		inputAttribs[1] = utils::InputAttribute(0, 1, VK_FORMAT_R32G32B32A32_SFLOAT, sizeof(float) * 3);
		inputAttribs[2] = utils::InputAttribute(0, 2, VK_FORMAT_R32G32_SFLOAT, sizeof(float) * 7);
		inputAttribs[3] = utils::InputAttribute(0, 3, VK_FORMAT_R32G32B32_SFLOAT, sizeof(float) * 9);
		inputAttribs[4] = utils::InputAttribute(0, 4, VK_FORMAT_R32G32B32_SFLOAT, sizeof(float) * 12);
		inputAttribs[5] = utils::InputAttribute(0, 5, VK_FORMAT_R32G32B32_SFLOAT, sizeof(float) * 15);
		return inputAttribs;
	}
}