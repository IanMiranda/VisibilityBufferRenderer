#pragma once

#include "API/GraphicsPipeline.h"
#include "API/Image.h"
#include "API/ImageView.h"
#include "API/PipelineLayout.h"
#include "BindlessSet.h"
#include "Common.h"
#include "DescriptorSetAllocator.h"

namespace im
{
    class Renderer;
    class Scene;

    struct FwObjectData
    {
        glm::mat4 model;
        VkDeviceAddress vertexBuffer;
        uint32_t albedoMapIndex;
        uint32_t metallicMapIndex;
        uint32_t roughnessMapIndex;
        uint32_t normalMapIndex;
        uint32_t aoMapIndex;
        uint32_t emissiveMapIndex;
    };

    struct FwMatrixData
    {
        glm::mat4 view;
        glm::mat4 viewProj;
        glm::mat4 viewProjLight;
        glm::mat4 viewInverse;
    };

    class ForwardBackend
    {
    public:
        ForwardBackend(Renderer &renderer, size_t maxFramesInFlight,
                       Image &depthImage);

        void BeginScene(Renderer &renderer, Scene &scene, CommandBuffer &cmd,
                        ImageView &depthView, uint32_t frameIndex);
        void DrawBatch(CommandBuffer &cmd, const std::vector<VbObject> &batch);
        void End(CommandBuffer &cmd, uint32_t frameIndex);

    private:
        TextureCube EquirectangularToCubemap(Renderer &renderer,
                                             Image &depthImage,
                                             ImageView &eqMap);
        TextureCube CalculateDiffuseIrradiance(Renderer &renderer,
                                               Image &depthImage,
                                               ImageView &cubeMap);
        TextureCube PrefilterEnvMap(Renderer &renderer, Image &depthImage,
                                    ImageView &cubeMap);
        Texture2D GenerateBrdfLut(Renderer &renderer, Image &depthImage);

        Buffer CreateCubeVertexBuffer(Renderer &renderer);

    private:
        BindlessSet mBindlessSet;
        DescriptorSetAllocator mSetAllocator;

        std::unique_ptr<DescriptorSetLayout> mMainLayout;
        std::unique_ptr<PipelineLayout> mMainPipeLayout;
        std::unique_ptr<GraphicsPipeline> mMainPipe;

        std::vector<std::unique_ptr<DescriptorSet>> mMainDescSets;

        TextureCube mEnvMap;
        std::unique_ptr<DescriptorSetLayout> mEnvMapSetLayout;
        std::unique_ptr<PipelineLayout> mEnvMapPipeLayout;
        std::unique_ptr<GraphicsPipeline> mEnvMapPipe;
        std::unique_ptr<DescriptorSet> mEnvMapSet;

        TextureCube mIrradianceMap;
        TextureCube mPrefilteredEnvMap;
        Texture2D mBrdfLut;

        std::vector<std::unique_ptr<Buffer>> mMatrixBuffers;
        std::vector<std::unique_ptr<Buffer>> mLightBuffers;
    };
} // namespace im