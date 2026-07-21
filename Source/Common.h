#pragma once

#include <iostream>
#include <vector>
#include <array>
#include <string>
#include <utility>
#include <memory>

#include <volk.h>
#include <GLFW/glfw3.h>
#include <vk_mem_alloc.h>
#include <glm/gtx/hash.hpp>
#include <glm/glm.hpp>
#include <fmt/base.h>
#include <fmt/std.h>

#define VK_CHECK(x) \
	do \
	{ \
		VkResult result = (x); \
		if (result != VK_SUCCESS) \
		{ \
			fmt::println(stderr, "Error at line {} in file {}: {}", __LINE__, __FILE__, static_cast<uint32_t>(result)); \
		} \
	} while(0)

namespace im
{
	struct InputBinding;
	
	struct Vertex
	{
		glm::vec3 position;
		glm::vec4 color;
		glm::vec2 uv;
		glm::vec3 normal;
		glm::vec3 tangent;
		glm::vec3 bitangent;

		bool operator<=>(const Vertex& other) const = default;

		static std::vector<InputBinding> GetInputBindings();
	};

	struct ShadowPassData
	{
		glm::mat4 mvp;
	};

	struct ObjectData
	{
		glm::mat4 model;
		uint32_t albedoMapIndex;
		uint32_t metallicMapIndex;
		uint32_t roughnessMapIndex;
		uint32_t normalMapIndex;
		uint32_t aoMapIndex;
		uint32_t emissiveMapIndex;
	};

	struct MainPassData
	{
		glm::mat4 view;
		glm::mat4 viewProj;
		glm::mat4 viewProjLight;
		glm::mat4 viewInverse;
		VkDeviceAddress objectData;
		VkDeviceAddress vertexData;
		uint32_t lightCount;
		uint32_t pad0;
	};

	struct CubemapData
	{
		glm::mat4 viewProjInverse;
		
		CubemapData(const glm::mat4& view, const glm::mat4& proj)
			: viewProjInverse(glm::inverse(proj * glm::mat4(glm::mat3(view)))) // Remove translations
		{
		}
	};

	struct EqMapData
	{
		glm::mat4 viewProj;

		EqMapData(const glm::mat4& viewProj) : viewProj(viewProj) {}
	};

	struct PrefilterData
	{
		glm::mat4 viewProj;
		float roughness;

		PrefilterData(const glm::mat4& viewProj, float roughness)
			: viewProj(viewProj), roughness(roughness)
		{}
	};

	class Buffer;
	class Texture2D;

	struct Material
	{
		std::shared_ptr<Texture2D> albedoMap;
		std::shared_ptr<Texture2D> metallicMap;
		std::shared_ptr<Texture2D> roughnessMap;
		std::shared_ptr<Texture2D> normalMap;
		std::shared_ptr<Texture2D> aoMap;
		std::shared_ptr<Texture2D> emissiveMap;
	};

	struct VbMesh
	{
		std::unique_ptr<Buffer> vertexBuffer;
		std::unique_ptr<Buffer> indexBuffer;
		uint32_t indexCount;

		VbMesh(
			std::unique_ptr<Buffer> vertexBuffer,
			std::unique_ptr<Buffer> indexBuffer,
			uint32_t indexCount
		);
	};

	struct VbObject
	{
		std::shared_ptr<VbMesh> mesh;
		Material material;
		glm::mat4 transform;

		VbObject(std::shared_ptr<VbMesh> mesh, Material material, const glm::mat4& transform)
			: mesh(mesh), material(material), transform(transform)
		{
		}
	};

	struct Mesh
	{
		std::vector<Vertex> vertices;
		std::vector<uint32_t> indices;
		uint32_t sceneBufferIndex;

		Mesh(
			const std::vector<Vertex>& vertices,
			const std::vector<uint32_t>& indices,
			uint32_t sceneBufferIndex)
			: vertices(vertices)
			, indices(indices)
			, sceneBufferIndex(sceneBufferIndex)
		{}
	};

	struct Object
	{
		std::shared_ptr<Mesh> mesh;
		Material material;
		glm::mat4 transform;

		Object(std::shared_ptr<Mesh> mesh, Material material, const glm::mat4& transform)
			: mesh(mesh), material(material), transform(transform)
		{
		}
	};
}

namespace std
{
	template<> struct hash<im::Vertex>
	{
		size_t operator()(const im::Vertex& vertex) const
		{
			return ((((hash<glm::vec3>()(vertex.position) ^
				(hash<glm::vec4>()(vertex.color) << 1)) >> 1) ^
				(hash<glm::vec2>()(vertex.uv) << 1) >> 1) ^
				(hash<glm::vec3>()(vertex.normal) << 1) >> 1) ^
				(hash<glm::vec3>()(vertex.tangent) << 1);
		}
	};
}
