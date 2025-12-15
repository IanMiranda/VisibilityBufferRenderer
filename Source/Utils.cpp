#include "Utils.h"

#include <fstream>
#include <algorithm>
#include <tiny_gltf.h>

#include "API/Buffer.h"

namespace im::utils
{
	VkVertexInputBindingDescription InputBinding(uint32_t binding, VkVertexInputRate rate, uint32_t stride)
	{
		VkVertexInputBindingDescription res{};
		res.binding = binding;
		res.inputRate = rate;
		res.stride = stride;
		return res;
	}

	VkVertexInputAttributeDescription InputAttribute(uint32_t binding, uint32_t location, VkFormat format, uint32_t offset)
	{
		VkVertexInputAttributeDescription res{};
		res.binding = binding;
		res.location = location;
		res.format = format;
		res.offset = offset;
		return res;
	}

	VkRect2D Scissor(VkExtent2D size)
	{
		VkRect2D res{};
		res.offset = { 0, 0 };
		res.extent = size;
		return res;
	}

	std::pair<VkViewport, VkRect2D> ViewportAndScissor(VkExtent2D size)
	{
		// Assume negative viewport height
		std::pair<VkViewport, VkRect2D> res;

		res.first.x = 0.0f;
		res.first.y = static_cast<float>(size.height);
		res.first.width = size.width;
		res.first.height = -static_cast<float>(size.height);
		res.first.minDepth = 0.0f;
		res.first.maxDepth = 1.0f;
		res.second = Scissor(size);
	
		return res;
	}

	std::vector<char> ReadFile(const std::filesystem::path& path)
	{
		std::ifstream file(path, std::ios::binary | std::ios::ate);
		if (!file.is_open())
		{
			fmt::println(stderr, "Error: failed to open file with path '{}'!", path);
			return {};
		}

		auto size = file.tellg();
		std::vector<char> res(size);
		file.seekg(0, std::ios::beg);
		file.read(res.data(), size);

		return res;
	}

	std::pair<std::vector<Vertex>, std::vector<uint32_t>> LoadModel(const std::filesystem::path& path)
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
			fmt::println("Failed to load model from path {}!", path);
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

	VkRenderingAttachmentInfo ColorAttachment(VkImageView view, VkAttachmentLoadOp load, VkAttachmentStoreOp store, VkClearValue clear)
	{
		VkRenderingAttachmentInfo colorAttach{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
		colorAttach.clearValue = clear;
		colorAttach.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		colorAttach.imageView = view;
		colorAttach.loadOp = load;
		colorAttach.storeOp = store;
		return colorAttach;
	}

	VkRenderingAttachmentInfo DepthAttachment(VkImageView view, VkAttachmentLoadOp load, VkAttachmentStoreOp store, VkClearValue clear)
	{
		VkRenderingAttachmentInfo depthAttach{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
		depthAttach.clearValue = clear;
		depthAttach.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		depthAttach.imageView = view;
		depthAttach.loadOp = load;
		depthAttach.storeOp = store;
		return depthAttach;
	}

	VkPushConstantRange PushConstantRange(VkShaderStageFlags stages, uint32_t size, uint32_t offset)
	{
		VkPushConstantRange pushRange{};
		pushRange.stageFlags = stages;
		pushRange.size = size;
		pushRange.offset = offset;
		return pushRange;
	}

	VkClearValue ClearColor(const glm::vec4& value)
	{
		VkClearValue res{};
		res.color = { value.r, value.g, value.b, value.a };
		return res;
	}

	VkClearValue ClearDepth(float depth, uint32_t stencil)
	{
		VkClearValue res{};
		res.depthStencil.depth = depth;
		res.depthStencil.stencil = stencil;
		return res;
	}
}
