#pragma once

#include "Common.h"

namespace im
{
	struct Mesh
	{
		std::unique_ptr<Buffer> vertexBuffer;
		std::unique_ptr<Buffer> indexBuffer;
		uint32_t indexCount;

		Mesh(std::unique_ptr<Buffer> vertexBuffer, std::unique_ptr<Buffer> indexBuffer, uint32_t indexCount);
	};

	struct MeshData // TODO: better name, perhaps remove old mesh struct?
	{
		std::unique_ptr<Buffer> vertexBuffer;
		std::unique_ptr<Buffer> meshBuffer;
		uint32_t meshletCount;
	};

	inline constexpr uint32_t MaxMeshletVertices = 64;
	inline constexpr uint32_t MaxMeshletTriangles = 126;
	inline constexpr uint32_t MaxMeshletIndices = MaxMeshletTriangles * 3;

	struct Meshlet
	{
		glm::vec3 coneAxis{ 0.0f, 0.0f, 0.0f };
		float coneAngle{ 0.0f };
		uint32_t vertices[MaxMeshletVertices];
		uint8_t indices[MaxMeshletIndices];
		uint8_t vertexCount{ 0 };
		uint8_t triangleCount{ 0 };
	};

	struct Object
	{
		MeshData* mesh;
		Material material;
		glm::mat4 transform;

		Object(MeshData* mesh, Material material, const glm::mat4& transform)
			: mesh(mesh), material(material), transform(transform)
		{
		}
	};

	std::pair<std::vector<Vertex>, std::vector<uint32_t>> LoadGltfModel(const std::filesystem::path& path);
	std::vector<Meshlet> BuildMeshlets(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices);
	void CalculateMeshletCones(std::vector<Meshlet>& meshlets, const std::vector<Vertex>& vertices);
}