#pragma once

#include <iostream>
#include <vector>
#include <array>
#include <string>
#include <utility>
#include <memory>

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
	struct Vertex
	{
		glm::vec3 position;
		glm::vec4 color;
		glm::vec2 uv;
		glm::vec3 normal;
		glm::vec3 tangent;
		glm::vec3 bitangent;

		bool operator<=>(const Vertex& other) const = default;

		static VkVertexInputBindingDescription GetInputBinding(uint32_t binding);
		static std::vector<VkVertexInputAttributeDescription> GetInputAttributes();
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
		uint32_t lightCount;
		uint32_t pad0;
		uint32_t pad1;
		uint32_t pad2;
	};

	struct GeomPassData
	{
		glm::mat4 view;
		glm::mat4 viewProj;

		GeomPassData(const glm::mat4& view, const glm::mat4 proj)
			: view(view), viewProj(proj* view)
		{
		}
	};

	struct LightingPassData
	{
		glm::mat4 viewInverse;
		glm::mat4 viewProjInverse;
		uint32_t lightCount;
		uint32_t pad0;
		uint32_t pad1;
		uint32_t pad2;
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

	struct Mesh
	{
		std::unique_ptr<Buffer> vertexBuffer;
		std::unique_ptr<Buffer> indexBuffer;
		uint32_t indexCount;
		Material material;
		glm::mat4 transform;
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
