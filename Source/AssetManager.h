#pragma once

#include "API/ImageView.h"
#include "Common.h"
#include "tiny_gltf.h"
#include <unordered_map>

namespace im
{
    class RenderDevice;

    class AssetManager
    {
    public:
        AssetManager(Device &device);

        std::vector<std::unique_ptr<Node>> LoadGltfScene(
            const std::filesystem::path &path);

        Texture2D GetOrLoadTextures();

    private:
        enum class DefaultTexture
        {
            BaseColor,
            MetallicRoughness,
            Emissive,
            Normal,
            Occlusion,
        };

    private:
        std::unique_ptr<Node> TraverseGltfNode(
            const tinygltf::Model &model, const tinygltf::Node &node,
            const glm::mat4 &globalTransform,
            std::unordered_map<int, std::shared_ptr<Mesh>> &loadedMeshes);

        std::shared_ptr<Mesh> LoadMesh(const tinygltf::Model &model,
                                       const tinygltf::Mesh &mesh);

        Material LoadMaterial(const tinygltf::Model &model,
                              const tinygltf::Material &material);

        std::shared_ptr<Texture2D> LoadTexture(
            const tinygltf::Model &model, int idx,
            std::shared_ptr<Texture2D> defaultTexture);

        VkFormat GetFormatFromGltf(int bits, int components);

        std::shared_ptr<Texture2D> CreateDefaultTexture(const glm::vec4 &color);

    private:
        Device &mDevice;
        std::vector<std::shared_ptr<Texture2D>> mDefaultTextures;
    };
} // namespace im