#include "Common.h"

#include "Utils.h"
#include "API/GraphicsPipeline.h"
#include "API/Buffer.h"

namespace im
{
	std::vector<InputBinding> Vertex::GetInputBindings()
	{
		return {
			InputBinding(
				{
					InputAttribute(0, VK_FORMAT_R32G32B32_SFLOAT, 0),
					InputAttribute(1, VK_FORMAT_R32G32B32A32_SFLOAT, sizeof(float) * 3),
					InputAttribute(2, VK_FORMAT_R32G32_SFLOAT, sizeof(float) * 7),
					InputAttribute(3, VK_FORMAT_R32G32B32_SFLOAT, sizeof(float) * 9),
					InputAttribute(4, VK_FORMAT_R32G32B32_SFLOAT, sizeof(float) * 12),
					InputAttribute(5, VK_FORMAT_R32G32B32_SFLOAT, sizeof(float) * 15)
				}, VK_VERTEX_INPUT_RATE_VERTEX, sizeof(Vertex))
		};
	}

	Mesh::Mesh(
		std::unique_ptr<Buffer> vertexBuffer,
		std::unique_ptr<Buffer> indexBuffer,
		uint32_t indexCount)
		: vertexBuffer(std::move(vertexBuffer))
		, indexBuffer(std::move(indexBuffer))
		, indexCount(indexCount)
	{
	}
}