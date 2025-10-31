#pragma once

#include <iostream>
#include <vector>
#include <array>
#include <string>

#include <GLFW/glfw3.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>

#define VK_CHECK(x) \
	do \
	{ \
		VkResult _result = (x); \
		if (_result != VK_SUCCESS) \
		{ \
			std::cerr << "Error at line " << __LINE__ << " in file " << __FILE__ << ": " << _result << '\n'; \
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

		bool operator==(const Vertex& other) const
		{
			return position == other.position
				&& color == other.color
				&& uv == other.uv
				&& normal == other.normal
				&& tangent == other.tangent
				&& bitangent == other.bitangent;
		}

		static std::vector<VkVertexInputAttributeDescription> GetInputAttributes();
	};

	struct ShadowPassData
	{
		glm::mat4 mvp;
	};

	struct ObjectData
	{
		glm::mat4 model;
		uint32_t diffuseMapHandle;
		uint32_t normalMapHandle;
	};

	struct GlobalPassData
	{
		glm::mat4 view;
		glm::mat4 viewProj;
		glm::mat4 viewProjLight;
		glm::mat4 viewInverse;
		glm::vec3 lightDir;
		float _pad0;
	};

	struct CubemapData
	{
		glm::mat4 viewProjInverse;
	};

	class Device;
	class Buffer;
	class Texture2D;

	struct Mesh
	{
		std::unique_ptr<Buffer> vertexBuffer;
		std::unique_ptr<Buffer> indexBuffer;
		uint32_t indexCount;
		glm::mat4 transform;
		std::shared_ptr<Texture2D> diffuseMap;
		std::shared_ptr<Texture2D> normalMap;
	};
}

namespace std
{
	template<> struct hash<im::Vertex>
	{
		size_t operator()(const im::Vertex& vertex) const
		{
			return 0; // TODO: change for actual hash
		}
	};
}
