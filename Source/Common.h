#pragma once

#include <ranges>

#include <memory>
#include <vector>

#include <volk.h>

#include <GLFW/glfw3.h>
#include <fmt/base.h>
#include <fmt/std.h>
#include <glm/glm.hpp>
#include <glm/gtx/hash.hpp>
#include <vk_mem_alloc.h>

#define VK_CHECK(x)                                                            \
    do                                                                         \
    {                                                                          \
        VkResult result = (x);                                                 \
        if (result != VK_SUCCESS)                                              \
        {                                                                      \
            fmt::println(stderr, "Error at line {} in file {}: {}", __LINE__,  \
                         __FILE__, static_cast<uint32_t>(result));             \
        }                                                                      \
    } while (0)

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

        bool operator==(const Vertex &other) const
        {
            return position == other.position && color == other.color &&
                   uv == other.uv && normal == other.normal &&
                   tangent == other.tangent && bitangent == other.bitangent;
        }
    };

    struct CubemapData
    {
        glm::mat4 viewProjInverse;

        CubemapData(const glm::mat4 &view, const glm::mat4 &proj)
            : viewProjInverse(glm::inverse(
                  proj * glm::mat4(glm::mat3(view)))) // Remove translations
        {
        }
    };

    struct EqMapData
    {
        glm::mat4 viewProj;

        EqMapData(const glm::mat4 &viewProj) : viewProj(viewProj)
        {
        }
    };

    struct PrefilterData
    {
        glm::mat4 viewProj;
        float roughness;

        PrefilterData(const glm::mat4 &viewProj, float roughness)
            : viewProj(viewProj), roughness(roughness)
        {
        }
    };

    class Buffer;
    class Texture2D;

    struct Material
    {
        std::shared_ptr<Texture2D> albedoMap;
        std::shared_ptr<Texture2D> metallicRoughnessMap;
        std::shared_ptr<Texture2D> normalMap;
        std::shared_ptr<Texture2D> aoMap;
        std::shared_ptr<Texture2D> emissiveMap;
    };

    struct Submesh
    {
        std::unique_ptr<Buffer> vertexBuffer;
        std::unique_ptr<Buffer> indexBuffer;
        uint32_t indexCount;
        Material material;
    };

    struct Mesh
    {
        std::vector<Submesh> submeshes;
    };

    struct Node
    {
        std::shared_ptr<Mesh> mesh;
        glm::mat4 transform;
        std::vector<std::unique_ptr<Node>> children;

        Node(std::shared_ptr<Mesh> mesh, const glm::mat4 &transform,
             std::vector<std::unique_ptr<Node>> children)
            : mesh(mesh), transform(transform), children(std::move(children))
        {
        }
    };

    struct RenderCommand
    {
        std::shared_ptr<Mesh> mesh;
        glm::mat4 transform;
    };
} // namespace im

namespace std
{
    template <> struct hash<im::Vertex>
    {
        size_t operator()(const im::Vertex &vertex) const
        {
            return ((((hash<glm::vec3>()(vertex.position) ^
                       (hash<glm::vec4>()(vertex.color) << 1)) >>
                      1) ^
                     (hash<glm::vec2>()(vertex.uv) << 1) >> 1) ^
                    (hash<glm::vec3>()(vertex.normal) << 1) >> 1) ^
                   (hash<glm::vec3>()(vertex.tangent) << 1);
        }
    };
} // namespace std
