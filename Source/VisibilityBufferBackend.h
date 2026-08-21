#pragma once

#include "API/Buffer.h"
#include "API/CommandBuffer.h"
#include "API/ComputePipeline.h"
#include "API/DescriptorSet.h"
#include "API/DescriptorSetLayout.h"
#include "API/GraphicsPipeline.h"
#include "API/Image.h"
#include "API/ImageView.h"
#include "API/PipelineLayout.h"
#include "BindlessSet.h"
#include "Common.h"
#include "DescriptorSetAllocator.h"
#include "Light.h"
#include "vulkan/vulkan_core.h"

namespace im
{
    class Renderer;
    class Scene;

    struct Texture2D;

    struct VbPassData
    {
        glm::mat4 modelViewProj;
        VkDeviceAddress vertexData;
    };

    struct VbWorkItem
    {
        uint32_t tileId;
        uint32_t shaderId;
    };

    struct VbBuildData
    {
        VkDeviceAddress instanceToShaderIdMap;
        VkDeviceAddress workListCounter;
        VkDeviceAddress workList;
        VkDeviceAddress shaderIdToTileCount;
        glm::uvec2 windowSize;
    };

    struct VbSortData
    {
        VkDeviceAddress worklistCounter;
        VkDeviceAddress workList;
        VkDeviceAddress shaderIdToTileCount;
        VkDeviceAddress offsetTable;
        VkDeviceAddress tileBuffer;
        VkDeviceAddress indirectBuffer;
        glm::uvec2 windowSize;
    };

    struct VbMaterialData
    {
        uint32_t albedoMapIndex;
        uint32_t metallicMapIndex;
        uint32_t roughnessMapIndex;
        uint32_t normalMapIndex;
        uint32_t aoMapIndex;
        uint32_t emissiveMapIndex;
    };

    struct VbShadingData
    {
        VkDeviceAddress instanceToShaderIdMap;
        VkDeviceAddress vertexBuffers;
        VkDeviceAddress indexBuffers;
        VkDeviceAddress transforms;
        VkDeviceAddress offsetTable;
        VkDeviceAddress tiles;
        VkDeviceAddress materials;
        glm::uvec2 screenSize;
        uint32_t shaderId;
    };

    struct VbDispatchIndirectCommand
    {
        uint32_t x;
        uint32_t y;
        uint32_t z;
        uint32_t pad;
    };

    struct VbConstantData
    {
        glm::mat4 view;
        glm::mat4 viewProj;
        glm::mat4 viewProjLight;
        glm::mat4 viewInverse;
    };

    inline constexpr int NumLights = 1024;

    struct VbLightData
    {
        PointLight lights[NumLights];
        uint32_t lightCount{0};
    };

    class VisibilityBufferBackend
    {
    public:
        VisibilityBufferBackend(Renderer &renderer, size_t maxFramesInFlight,
                                Image &depthImage);

        void BeginScene(Scene &scene, CommandBuffer &cmd, ImageView &depthView,
                        uint32_t frameIndex);
        void DrawBatch(Scene &scene, CommandBuffer &cmd,
                       const std::vector<VbObject> &objects,
                       uint32_t frameIndex);
        void End(Renderer &renderer, Scene &scene, CommandBuffer &cmd,
                 ImageView &depthView, uint32_t frameIndex);

        void ResizeBuffers(Renderer &renderer, size_t maxFramesInFlight);

    private:
        std::vector<Texture2D> InitVisBuffers(Renderer &renderer, size_t count);
        std::vector<std::unique_ptr<Buffer>> InitBuffers(
            Renderer &renderer, size_t count, const BufferDesc &desc);
        std::vector<std::unique_ptr<DescriptorSet>> InitDescSets(
            Renderer &renderer, size_t count);

        static uint32_t GetTileCount(VkExtent2D extent);

    private:
        static constexpr uint32_t MaxDrawCalls{0x40000};
        static constexpr uint32_t MaxShaders{64};
        static constexpr glm::uvec2 TileSize{16, 16};
        static constexpr uint32_t GroupSize{256};

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
        DescriptorSetAllocator mSetAllocator;

        PipelineLayout mVisPipeLayout;
        GraphicsPipeline mVisPipe;
        BindlessSet mBindlessSet;

        DescriptorSetLayout mWorkListDsl;
        PipelineLayout mWorkListPipeLayout;
        ComputePipeline mWorkListPipe;

        DescriptorSetLayout mSortDsl;
        PipelineLayout mSortPipeLayout;
        ComputePipeline mSortPipe;

        DescriptorSetLayout mShadeDsl;
        DescriptorSetLayout mShadeLightDsl;
        PipelineLayout mShadePipeLayout;
        ComputePipeline mShadePipe;

        std::vector<Texture2D> mVisBuffers;
        std::vector<std::unique_ptr<Buffer>> mInstanceToShaderIdMaps;
        std::vector<std::unique_ptr<Buffer>> mWorkListCounters;
        std::vector<std::unique_ptr<Buffer>> mWorkLists;
        std::vector<std::unique_ptr<Buffer>> mShaderIdToTileCounts;
        std::vector<std::unique_ptr<Buffer>> mOffsetTables;
        std::vector<std::unique_ptr<Buffer>> mTileBuffers;
        std::vector<std::unique_ptr<Buffer>> mVertexBuffers;
        std::vector<std::unique_ptr<Buffer>> mIndexBuffers;
        std::vector<std::unique_ptr<Buffer>> mTransformBuffers;
        std::vector<std::unique_ptr<Buffer>> mMaterialBuffers;
        std::vector<std::unique_ptr<Buffer>> mIndirectBuffers;
        std::vector<std::unique_ptr<Buffer>> mShadeConstants;
        std::vector<std::unique_ptr<Buffer>> mLightData;
        std::vector<std::unique_ptr<DescriptorSet>> mWorkListDescSets;
        std::vector<std::unique_ptr<DescriptorSet>> mLightDescSets;

        TextureCube mEnvMap;
        TextureCube mIrradianceMap;
        TextureCube mPrefilteredEnvMap;
        Texture2D mBrdfLut;

        std::unique_ptr<DescriptorSetLayout> mEnvMapSetLayout;
        std::unique_ptr<PipelineLayout> mEnvMapPipeLayout;
        std::unique_ptr<GraphicsPipeline> mEnvMapPipe;
        std::unique_ptr<DescriptorSet> mEnvMapSet;

        uint32_t mCurrentInstance{1};

        uint32_t *mInstanceToShaderIdMapPtr{nullptr};
        VkDeviceAddress *mVertexBuffersPtr{nullptr};
        VkDeviceAddress *mIndexBuffersPtr{nullptr};
        float *mTransformBufferPtr{nullptr};
        VbMaterialData *mMaterialBufferPtr{nullptr};
    };
} // namespace im