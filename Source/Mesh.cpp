#include "Mesh.h"

#include <numeric>
#include <tiny_gltf.h>

#include "API/Buffer.h"

namespace im
{
	Mesh::Mesh(
		std::unique_ptr<Buffer> vertexBuffer,
		std::unique_ptr<Buffer> indexBuffer,
		uint32_t indexCount)
		: vertexBuffer(std::move(vertexBuffer))
		, indexBuffer(std::move(indexBuffer))
		, indexCount(indexCount)
	{
	}

	std::pair<std::vector<Vertex>, std::vector<uint32_t>> LoadGltfModel(const std::filesystem::path& path)
	{
		tinygltf::Model model;
		tinygltf::TinyGLTF loader;

		std::string error, warn;

		std::vector<Vertex> vertices;
		std::vector<uint32_t> indices;

		bool res = loader.LoadASCIIFromFile(&model, &error, &warn, path.string().c_str());
		if (!warn.empty())
			fmt::println(stderr, "GLTF warning: {}", warn);

		if (!error.empty())
			fmt::println(stderr, "GLTF error: {}", error);

		if (!res)
		{
			fmt::println(stderr, "Failed to load model from path {}!", path);
			return {};
		}

		static std::size_t maxVertexIndex = 0;
		std::unordered_map<Vertex, uint32_t> uniqueVertices;
		for (const auto& mesh : model.meshes)
		{
			for (const auto& prim : mesh.primitives)
			{
				// Indices
				const tinygltf::Accessor& indexAccessor = model.accessors[prim.indices];
				const tinygltf::BufferView& indexBufferView = model.bufferViews[indexAccessor.bufferView];
				const tinygltf::Buffer& indexBuffer = model.buffers[indexBufferView.buffer];

				// Vertex positions
				const tinygltf::Accessor& posAccessor = model.accessors[prim.attributes.at("POSITION")];
				const tinygltf::BufferView& posBufferView = model.bufferViews[posAccessor.bufferView];
				const tinygltf::Buffer& posBuffer = model.buffers[posBufferView.buffer];

				bool hasTexCoord = prim.attributes.find("TEXCOORD_0") != prim.attributes.end();
				const tinygltf::Accessor* texCoordAccessor = nullptr;
				const tinygltf::BufferView* texCoordBufferView = nullptr;
				const tinygltf::Buffer* texCoordBuffer = nullptr;

				// Normals
				const tinygltf::Accessor& normalAccessor = model.accessors[prim.attributes.at("NORMAL")];
				const tinygltf::BufferView& normalBufferView = model.bufferViews[normalAccessor.bufferView];
				const tinygltf::Buffer& normalBuffer = model.buffers[normalBufferView.buffer];

				// Tangent
				const tinygltf::Accessor& tangentAccessor = model.accessors[prim.attributes.at("TANGENT")];
				const tinygltf::BufferView& tangentBufferView = model.bufferViews[tangentAccessor.bufferView];
				const tinygltf::Buffer& tangentBuffer = model.buffers[tangentBufferView.buffer];

				if (hasTexCoord)
				{
					texCoordAccessor = &model.accessors[prim.attributes.at("TEXCOORD_0")];
					texCoordBufferView = &model.bufferViews[texCoordAccessor->bufferView];
					texCoordBuffer = &model.buffers[texCoordBufferView->buffer];
				}

				for (size_t i = 0; i < posAccessor.count; ++i)
				{
					Vertex v{};
					const float* pos = reinterpret_cast<const float*>(&posBuffer.data[posBufferView.byteOffset + posAccessor.byteOffset + i * 12]);
					v.position = { pos[0], pos[1], pos[2] };

					if (hasTexCoord)
					{
						const float* uv = reinterpret_cast<const float*>(&texCoordBuffer->data[texCoordBufferView->byteOffset + texCoordAccessor->byteOffset + i * 8]);
						v.uv = { uv[0], 1.0f - uv[1] };
					}

					v.color = glm::vec4(1.0f);

					const float* normal = reinterpret_cast<const float*>(&normalBuffer.data[normalBufferView.byteOffset + normalAccessor.byteOffset + i * 12]);
					v.normal = { normal[0], normal[1], normal[2] };

					const float* tangent = reinterpret_cast<const float*>(&tangentBuffer.data[tangentBufferView.byteOffset + tangentAccessor.byteOffset + i * 16]);
					v.tangent = { tangent[0], tangent[1], tangent[2] };
					v.bitangent = glm::cross(v.normal, v.tangent) * tangent[3]; // https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html

					uniqueVertices[v] = static_cast<uint32_t>(vertices.size());
					vertices.push_back(v);
				}

				const void* indexData = &indexBuffer.data[indexBufferView.byteOffset + indexAccessor.byteOffset];
				if (indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT)
				{
					const uint16_t* indexPtr = reinterpret_cast<const uint16_t*>(indexData);
					for (size_t i = 0; i < indexAccessor.count; ++i)
					{
						Vertex v = vertices[indexPtr[i]];
						indices.push_back(uniqueVertices[v]);
					}
				}
				else if (indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT)
				{
					const uint32_t* indexPtr = reinterpret_cast<const uint32_t*>(indexData);
					for (size_t i = 0; i < indexAccessor.count; ++i)
					{
						Vertex v = vertices[indexPtr[i]];
						indices.push_back(uniqueVertices[v]);
					}
				}
				else if (indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE)
				{
					const uint8_t* indexPtr = reinterpret_cast<const uint8_t*>(indexData);
					for (size_t i = 0; i < indexAccessor.count; ++i)
					{
						Vertex v = vertices[indexPtr[i]];
						indices.emplace_back(uniqueVertices[v]);
					}
				}
			}
		}

		return { vertices, indices };
	}

	std::vector<Meshlet> BuildMeshlets(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices)
	{
		std::vector<Meshlet> res;

		Meshlet currentMeshlet{};
		std::vector<uint8_t> vertexToIndex(vertices.size());
		std::fill(vertexToIndex.begin(), vertexToIndex.end(), 0xFF);

		for (size_t i = 0; i < indices.size(); i += 3)
		{
			uint32_t i1 = indices[i];
			uint32_t i2 = indices[i + 1];
			uint32_t i3 = indices[i + 2];

			auto& v1 = vertexToIndex[i1];
			auto& v2 = vertexToIndex[i2];
			auto& v3 = vertexToIndex[i3];

			bool tooManyVerts = (v1 == 0xff) + (v2 == 0xff) + (v3 == 0xff) + currentMeshlet.vertexCount > MaxMeshletVertices;

			if (currentMeshlet.triangleCount == MaxMeshletTriangles || (tooManyVerts))
			{
				res.push_back(currentMeshlet);
				currentMeshlet = {};
				std::fill(vertexToIndex.begin(), vertexToIndex.end(), 0xFF);
			}

			if (v1 == 0xff)
			{
				v1 = currentMeshlet.vertexCount;
				currentMeshlet.vertices[currentMeshlet.vertexCount++] = i1;
			}
			if (v2 == 0xff)
			{
				v2 = currentMeshlet.vertexCount;
				currentMeshlet.vertices[currentMeshlet.vertexCount++] = i2;
			}
			if (v3 == 0xff)
			{
				v3 = currentMeshlet.vertexCount;
				currentMeshlet.vertices[currentMeshlet.vertexCount++] = i3;
			}

			currentMeshlet.indices[currentMeshlet.triangleCount * 3] = v1;
			currentMeshlet.indices[currentMeshlet.triangleCount * 3 + 1] = v2;
			currentMeshlet.indices[currentMeshlet.triangleCount * 3 + 2] = v3;
			currentMeshlet.triangleCount++;
		}

		if (currentMeshlet.triangleCount > 0)
		{
			res.push_back(currentMeshlet);
		}

		CalculateMeshletCones(res, vertices);
		return res;
	}

	void CalculateMeshletCones(std::vector<Meshlet>& meshlets, const std::vector<Vertex>& vertices)
	{
		for (auto& meshlet : meshlets)
		{
			std::vector<glm::vec3> triangleNormals(meshlet.triangleCount);
			for (size_t i = 0; i < meshlet.triangleCount; ++i)
			{
				const auto& v0 = vertices[meshlet.vertices[meshlet.indices[i * 3]]];
				const auto& v1 = vertices[meshlet.vertices[meshlet.indices[i * 3 + 1]]];
				const auto& v2 = vertices[meshlet.vertices[meshlet.indices[i * 3 + 2]]];

				const auto dir0 = v1.position - v0.position;
				const auto dir1 = v2.position - v0.position;

				triangleNormals[i] = glm::normalize(glm::cross(dir0, dir1));
			}

			const auto avgNormal = glm::normalize(
				std::accumulate(
					triangleNormals.begin(),
					triangleNormals.end(),
					glm::vec3(0.0f))
			);

			const auto minAngle = *std::min_element(
				triangleNormals.begin(),
				triangleNormals.end(),
				[avgNormal](const glm::vec3& normal, const glm::vec3& smallest) { return glm::dot(normal, avgNormal) < glm::dot(smallest, avgNormal); });

			meshlet.coneAxis = avgNormal;
			meshlet.coneAngle = glm::dot(minAngle, avgNormal);
		}
	}
}