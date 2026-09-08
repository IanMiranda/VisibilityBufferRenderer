#include "AssetManager.h"

#include "API/Buffer.h"
#include "API/CommandBuffer.h"
#include "API/Device.h"
#include "API/ImageView.h"
#include "Common.h"
#include "fmt/base.h"
#include "tiny_gltf.h"
#include "vulkan/vulkan_core.h"
#include <glm/gtc/type_ptr.hpp>
#include <memory>
#include <utility>

namespace im
{
    AssetManager::AssetManager(Device &device) : mDevice(device)
    {
        // TODO: actually use default textures
        mDefaultTextures.reserve(5);
        mDefaultTextures.emplace_back(
            CreateDefaultTexture(glm::vec4(0.5f, 0.5f, 0.5f, 1.0f)));
        mDefaultTextures.emplace_back(
            CreateDefaultTexture(glm::vec4(0.0f, 0.5f, 0.0f, 1.0f)));
        mDefaultTextures.emplace_back(
            CreateDefaultTexture(glm::vec4(0.0f, 0.0f, 0.0f, 1.0f)));
        mDefaultTextures.emplace_back(
            CreateDefaultTexture(glm::vec4(0.0f, 0.0f, 1.0f, 1.0f)));
        mDefaultTextures.emplace_back(
            CreateDefaultTexture(glm::vec4(0.0f, 0.0f, 0.0f, 1.0f)));
    }

    std::vector<std::unique_ptr<Node>> AssetManager::LoadGltfScene(
        const std::filesystem::path &path)
    {
        tinygltf::Model model;
        tinygltf::TinyGLTF loader;

        std::string error, warn;

        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;

        bool loaded = loader.LoadBinaryFromFile(&model, &error, &warn,
                                                path.string().c_str());
        if (!warn.empty())
            fmt::println(stderr, "GLTF warning: {}", warn);

        if (!error.empty())
            fmt::println(stderr, "GLTF error: {}", error);

        if (!loaded)
        {
            fmt::println(stderr, "Failed to load model from path {}!", path);
            return {};
        }

        // TODO: support loading more than just the default scene?
        std::unordered_map<int, std::shared_ptr<Mesh>> loadedMeshes;
        loadedMeshes[-1] = nullptr;
        std::vector<std::unique_ptr<Node>> res;
        for (const auto nodeIdx : model.scenes[0].nodes)
        {
            const auto &node = model.nodes[nodeIdx];

            res.push_back(
                TraverseGltfNode(model, node, glm::mat4(1.0f), loadedMeshes));
        }

        return res;
    }

    std::unique_ptr<Node> AssetManager::TraverseGltfNode(
        const tinygltf::Model &model, const tinygltf::Node &node,
        const glm::mat4 &globalTransform,
        std::unordered_map<int, std::shared_ptr<Mesh>> &loadedMeshes)
    {
        const auto localTransform = [&node]() {
            if (!node.matrix.empty())
            {
                return glm::mat4(glm::make_mat4(node.matrix.data()));
            }
            else
            {
                const auto translate =
                    node.translation.empty()
                        ? glm::mat4(1.0)
                        : glm::translate(glm::mat4(1.0f),
                                         glm::vec3(glm::make_vec3(
                                             node.translation.data())));
                const auto rotate =
                    node.rotation.empty()
                        ? glm::mat4(1.0f)
                        : glm::mat4_cast(
                              glm::quat(glm::make_quat(node.rotation.data())));
                const auto scale =
                    node.scale.empty()
                        ? glm::mat4(1.0f)
                        : glm::scale(glm::mat4(1.0f), glm::vec3(glm::make_vec3(
                                                          node.scale.data())));
                return translate * rotate * scale;
            }
        }();

        if (!loadedMeshes.contains(node.mesh))
        {
            loadedMeshes[node.mesh] = LoadMesh(model, model.meshes[node.mesh]);
        }

        const auto currentTransform = globalTransform * localTransform;

        std::vector<std::unique_ptr<Node>> children;
        children.reserve(node.children.size());
        for (const auto nodeIdx : node.children)
        {
            children.emplace_back(TraverseGltfNode(
                model, model.nodes[nodeIdx], currentTransform, loadedMeshes));
        }

        return std::make_unique<Node>(loadedMeshes[node.mesh], currentTransform,
                                      std::move(children));
    }

    std::shared_ptr<Mesh> AssetManager::LoadMesh(const tinygltf::Model &model,
                                                 const tinygltf::Mesh &mesh)
    {
        std::vector<Submesh> submeshes;

        for (const auto &prim : mesh.primitives)
        {
            // Indices
            const tinygltf::Accessor &indexAccessor =
                model.accessors[prim.indices];
            const tinygltf::BufferView &indexBufferView =
                model.bufferViews[indexAccessor.bufferView];
            const tinygltf::Buffer &indexBuffer =
                model.buffers[indexBufferView.buffer];

            // Vertex positions
            const tinygltf::Accessor &posAccessor =
                model.accessors[prim.attributes.at("POSITION")];
            const tinygltf::BufferView &posBufferView =
                model.bufferViews[posAccessor.bufferView];
            const tinygltf::Buffer &posBuffer =
                model.buffers[posBufferView.buffer];

            const bool hasTexCoord = prim.attributes.contains("TEXCOORD_0");
            const tinygltf::Accessor *texCoordAccessor = nullptr;
            const tinygltf::BufferView *texCoordBufferView = nullptr;
            const tinygltf::Buffer *texCoordBuffer = nullptr;

            // Normals
            const tinygltf::Accessor &normalAccessor =
                model.accessors[prim.attributes.at("NORMAL")];
            const tinygltf::BufferView &normalBufferView =
                model.bufferViews[normalAccessor.bufferView];
            const tinygltf::Buffer &normalBuffer =
                model.buffers[normalBufferView.buffer];

            // Tangent
            const bool hasTangent = prim.attributes.contains("TANGENT");
            tinygltf::Accessor *tangentAccessor = nullptr;
            tinygltf::BufferView *tangentBufferView = nullptr;
            tinygltf::Buffer *tangentBuffer = nullptr;

            if (hasTexCoord)
            {
                texCoordAccessor =
                    &model.accessors[prim.attributes.at("TEXCOORD_0")];
                texCoordBufferView =
                    &model.bufferViews[texCoordAccessor->bufferView];
                texCoordBuffer = &model.buffers[texCoordBufferView->buffer];
            }

            if (hasTangent)
            {
                *tangentAccessor =
                    model.accessors[prim.attributes.at("TANGENT")];
                *tangentBufferView =
                    model.bufferViews[tangentAccessor->bufferView];
                *tangentBuffer = model.buffers[tangentBufferView->buffer];
            }

            std::vector<Vertex> vertices;
            std::vector<uint32_t> indices;
            std::unordered_map<Vertex, uint32_t> vertexToIndex;

            for (size_t i = 0; i < posAccessor.count; ++i)
            {
                Vertex v{};
                const float *pos = reinterpret_cast<const float *>(
                    &posBuffer.data[posBufferView.byteOffset +
                                    posAccessor.byteOffset + i * 12]);
                v.position = {pos[0], pos[1], pos[2]};

                if (hasTexCoord)
                {
                    const float *uv = reinterpret_cast<const float *>(
                        &texCoordBuffer
                             ->data[texCoordBufferView->byteOffset +
                                    texCoordAccessor->byteOffset + i * 8]);
                    v.uv = {uv[0], 1.0f - uv[1]};
                }

                v.color = glm::vec4(1.0f);

                const float *normal = reinterpret_cast<const float *>(
                    &normalBuffer.data[normalBufferView.byteOffset +
                                       normalAccessor.byteOffset + i * 12]);
                v.normal = {normal[0], normal[1], normal[2]};

                if (hasTangent)
                {
                    const float *tangent = reinterpret_cast<const float *>(
                        &tangentBuffer
                             ->data[tangentBufferView->byteOffset +
                                    tangentAccessor->byteOffset + i * 16]);
                    v.tangent = {tangent[0], tangent[1], tangent[2]};
                    v.bitangent =
                        glm::cross(v.normal, v.tangent) *
                        tangent
                            [3]; // https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html
                }

                vertexToIndex[v] = static_cast<uint32_t>(vertices.size());
                vertices.push_back(v);
            }

            const void *indexData =
                &indexBuffer.data[indexBufferView.byteOffset +
                                  indexAccessor.byteOffset];
            if (indexAccessor.componentType ==
                TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT)
            {
                const uint16_t *indexPtr =
                    reinterpret_cast<const uint16_t *>(indexData);
                for (size_t i = 0; i < indexAccessor.count; ++i)
                {
                    Vertex v = vertices[indexPtr[i]];
                    indices.push_back(vertexToIndex[v]);
                }
            }
            else if (indexAccessor.componentType ==
                     TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT)
            {
                const uint32_t *indexPtr =
                    reinterpret_cast<const uint32_t *>(indexData);
                for (size_t i = 0; i < indexAccessor.count; ++i)
                {
                    Vertex v = vertices[indexPtr[i]];
                    indices.push_back(vertexToIndex[v]);
                }
            }
            else if (indexAccessor.componentType ==
                     TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE)
            {
                const uint8_t *indexPtr =
                    reinterpret_cast<const uint8_t *>(indexData);
                for (size_t i = 0; i < indexAccessor.count; ++i)
                {
                    Vertex v = vertices[indexPtr[i]];
                    indices.emplace_back(vertexToIndex[v]);
                }
            }

            if (!hasTangent)
            {
                for (size_t i = 0; i < indices.size(); i += 3)
                {
                    auto &v0 = vertices[indices[i]];
                    auto &v1 = vertices[indices[i + 1]];
                    auto &v2 = vertices[indices[i + 2]];

                    const glm::vec3 e1 = v1.position - v0.position;
                    const glm::vec3 e2 = v2.position - v0.position;

                    const glm::vec2 dUv1 = v1.uv - v0.uv;
                    const glm::vec2 dUv2 = v2.uv - v0.uv;

                    const float f = 1.0f / (dUv1.x * dUv2.y - dUv2.x * dUv1.y);

                    const glm::vec3 tangent = f * (dUv2.y * e1 - dUv1.y * e2);
                    const glm::vec3 bitangent =
                        f * (-dUv2.x * e1 + dUv1.x * e2);
                    v0.tangent = tangent;
                    v1.tangent = tangent;
                    v2.tangent = tangent;

                    v0.bitangent = bitangent;
                    v1.bitangent = bitangent;
                    v2.bitangent = bitangent;
                }
            }

            Buffer stageVbo(
                mDevice,
                BufferDesc::Upload(vertices.size() * sizeof(vertices[0])),
                vertices.data());
            Buffer stageIbo(
                mDevice,
                BufferDesc::Upload(indices.size() * sizeof(indices[0])),
                indices.data());
            auto vbo = std::make_unique<Buffer>(
                mDevice,
                BufferDesc(stageVbo.GetSize(),
                           VK_BUFFER_USAGE_2_TRANSFER_DST_BIT |
                               VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT |
                               VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT));
            auto ibo = std::make_unique<Buffer>(
                mDevice,
                BufferDesc(stageIbo.GetSize(),
                           VK_BUFFER_USAGE_2_TRANSFER_DST_BIT |
                               VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT |
                               VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT |
                               VK_BUFFER_USAGE_2_INDEX_BUFFER_BIT));

            mDevice.RunImmediateCommands([&](CommandBuffer &cmd) {
                cmd.Copy(stageVbo, *vbo);
                cmd.Copy(stageIbo, *ibo);
            });

            assert(prim.material > -1);

            Submesh res;
            res.vertexBuffer = std::move(vbo);
            res.indexBuffer = std::move(ibo);
            res.indexCount = indices.size();
            res.material = LoadMaterial(model, model.materials[prim.material]);
            submeshes.emplace_back(std::move(res));
        }

        return std::make_shared<Mesh>(std::move(submeshes));
    }

    Material AssetManager::LoadMaterial(const tinygltf::Model &model,
                                        const tinygltf::Material &material)
    {
        // TODO: process factors?
        Material res;
        res.albedoMap = LoadTexture(
            model, material.pbrMetallicRoughness.baseColorTexture.index,
            mDefaultTextures[static_cast<int>(DefaultTexture::BaseColor)]);

        res.metallicRoughnessMap = LoadTexture(
            model, material.pbrMetallicRoughness.metallicRoughnessTexture.index,
            mDefaultTextures[static_cast<int>(
                DefaultTexture::MetallicRoughness)]);

        res.emissiveMap = LoadTexture(
            model, material.emissiveTexture.index,
            mDefaultTextures[static_cast<int>(DefaultTexture::Emissive)]);

        res.normalMap = LoadTexture(model, material.normalTexture.index,
                                    mDefaultTextures[static_cast<int>(
                                        AssetManager::DefaultTexture::Normal)]);

        res.aoMap = LoadTexture(model, material.occlusionTexture.index,
                                mDefaultTextures[static_cast<int>(
                                    AssetManager::DefaultTexture::Occlusion)]);

        return res;
    }

    std::shared_ptr<Texture2D> AssetManager::LoadTexture(
        const tinygltf::Model &model, int idx,
        std::shared_ptr<Texture2D> defaultTexture)
    {
        // Have to load a new texture
        // TODO: add support for other samplers
        if (idx == -1)
        {
            return defaultTexture;
        }

        const auto &texture = model.textures[idx];

        if (texture.source == -1)
        {
            return defaultTexture;
        }

        const auto &image = model.images[texture.source];

        auto img = std::make_unique<Image>(
            mDevice, GetFormatFromGltf(image.bits, image.component),
            VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            image.width, image.height, 1, 1, VK_IMAGE_TYPE_2D, 1);
        auto imgView =
            std::make_unique<ImageView>(mDevice, *img, VK_IMAGE_VIEW_TYPE_2D,
                                        VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1);
        Buffer staging(mDevice, BufferDesc::Upload(image.image.size()),
                       image.image.data());
        mDevice.RunImmediateCommands([&](CommandBuffer &cmd) {
            cmd.Barrier(
                {},
                {ImageMemoryBarrier(
                    *img, VK_IMAGE_LAYOUT_UNDEFINED, VK_PIPELINE_STAGE_2_NONE,
                    VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_PIPELINE_STAGE_2_COPY_BIT,
                    VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_ASPECT_COLOR_BIT)},
                {});
            cmd.Copy(staging, *img, VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0);
            cmd.Barrier(
                {},
                {ImageMemoryBarrier(*img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                    VK_PIPELINE_STAGE_2_COPY_BIT,
                                    VK_ACCESS_2_TRANSFER_WRITE_BIT,
                                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                    VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |
                                        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                                    VK_ACCESS_2_SHADER_READ_BIT,
                                    VK_IMAGE_ASPECT_COLOR_BIT)},
                {});
        });

        auto res = std::make_shared<Texture2D>();
        res->image = std::move(img);
        res->view = std::move(imgView);
        return res;
    }

    VkFormat AssetManager::GetFormatFromGltf(int bits, int components)
    {
        switch (bits)
        {
        case 8: {
            switch (components)
            {
            case 1:
                return VK_FORMAT_R8_UNORM;
            case 2:
                return VK_FORMAT_R8G8_UNORM;
            case 3:
                return VK_FORMAT_R8G8B8_UNORM;
            case 4:
                return VK_FORMAT_R8G8B8A8_UNORM;
            }
        }
        case 16: {
            switch (components)
            {
            case 1:
                return VK_FORMAT_R16_SFLOAT;
            case 2:
                return VK_FORMAT_R16G16_SFLOAT;
            case 3:
                return VK_FORMAT_R16G16B16_SFLOAT;
            case 4:
                return VK_FORMAT_R16G16B16A16_SFLOAT;
            }
        }
        };

        fmt::println(stderr,
                     "AssetManager::GetFormatFromGltf: Invalid "
                     "number of bits {}!",
                     bits);
        assert(false);
    }

    std::shared_ptr<Texture2D> AssetManager::CreateDefaultTexture(
        const glm::vec4 &color)
    {
        auto img = std::make_unique<Image>(mDevice, VK_FORMAT_R8G8B8A8_UNORM,
                                           VK_IMAGE_USAGE_SAMPLED_BIT |
                                               VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                                           1, 1, 1, 1, VK_IMAGE_TYPE_2D, 1);
        auto imgView =
            std::make_unique<ImageView>(mDevice, *img, VK_IMAGE_VIEW_TYPE_2D,
                                        VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1);
        Buffer staging(mDevice, BufferDesc::Upload(sizeof(color)),
                       glm::value_ptr(color));
        mDevice.RunImmediateCommands([&](CommandBuffer &cmd) {
            cmd.Barrier(
                {},
                {ImageMemoryBarrier(
                    *img, VK_IMAGE_LAYOUT_UNDEFINED, VK_PIPELINE_STAGE_2_NONE,
                    VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_PIPELINE_STAGE_2_COPY_BIT,
                    VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_ASPECT_COLOR_BIT)},
                {});
            cmd.Copy(staging, *img, VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0);
            cmd.Barrier(
                {},
                {ImageMemoryBarrier(*img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                    VK_PIPELINE_STAGE_2_COPY_BIT,
                                    VK_ACCESS_2_TRANSFER_WRITE_BIT,
                                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                    VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |
                                        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                                    VK_ACCESS_2_SHADER_READ_BIT,
                                    VK_IMAGE_ASPECT_COLOR_BIT)},
                {});
        });

        auto res = std::make_shared<Texture2D>();
        res->image = std::move(img);
        res->view = std::move(imgView);
        return res;
    }

} // namespace im