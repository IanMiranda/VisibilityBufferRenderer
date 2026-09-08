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

    struct DfObjectData
    {
        glm::mat4 modelView;
        glm::mat4 modelViewProj;
        VkDeviceAddress vertexBuffer;
        uint32_t albedoMapIndex;
        uint32_t metallicRoughnessMapIndex;
        uint32_t normalMapIndex;
        uint32_t aoMapIndex;
        uint32_t emissiveMapIndex;
    };

    struct DfMatrixData
    {
        glm::mat4 view;
        glm::mat4 viewProj;
        glm::mat4 viewProjLight;
        glm::mat4 viewInverse;
    };

    class DeferredBackend
    {
    public:
        DeferredBackend(Renderer &renderer, size_t maxFramesInFlight,
                        Image &depthImage);

        void BeginScene(Renderer &renderer, Scene &scene, CommandBuffer &cmd,
                        ImageView &depthView, uint32_t frameIndex);
        void DrawBatch(Scene &scene, CommandBuffer &cmd,
                       const std::vector<RenderCommand> &cmds);
        void End(Renderer &renderer, Scene &scene, CommandBuffer &cmd,
                 ImageView &depthView, uint32_t frameIndex);

        void ResizeBuffers(Renderer &renderer, size_t maxFramesInFlight);

    private:
        struct GBuffer
        {
            Texture2D positionMetallic;
            Texture2D albedoRoughness;
            Texture2D normalAo;
            Texture2D emissive;
        };

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

        GBuffer CreateGBuffer(Renderer &renderer);

    private:
        BindlessSet mBindlessSet;
        DescriptorSetAllocator mSetAllocator;

        std::unique_ptr<PipelineLayout> mGeomPipeLayout;
        std::unique_ptr<GraphicsPipeline> mGeomPipe;

        std::unique_ptr<DescriptorSetLayout> mLightLayout;
        std::unique_ptr<PipelineLayout> mLightPipeLayout;
        std::unique_ptr<GraphicsPipeline> mLightPipe;

        std::vector<std::unique_ptr<DescriptorSet>> mLightDescSets;

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

        std::vector<GBuffer> mGBuffers;
    };
} // namespace im