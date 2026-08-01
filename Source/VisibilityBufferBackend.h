#pragma once

#include "API/Buffer.h"
#include "API/CommandBuffer.h"
#include "API/ComputePipeline.h"
#include "API/DescriptorSet.h"
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
        glm::uvec2 windowSize;
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
        uint32_t shaderId;
    };

    class VisibilityBufferBackend
    {
    public:
        VisibilityBufferBackend(Renderer &renderer, size_t maxFramesInFlight,
                                Image &depthImage);

        void BeginScene(CommandBuffer &cmd, ImageView &depthView,
                        uint32_t frameIndex);
        void DrawBatch(Scene &scene, CommandBuffer &cmd,
                       const std::vector<VbObject> &objects);
        void End(Renderer &renderer, CommandBuffer &cmd, uint32_t frameIndex);

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
        PipelineLayout mShadePipeLayout;
        ComputePipeline mShadePipe;

        std::vector<Texture2D> mVisBuffers;
        std::vector<std::unique_ptr<Buffer>> mInstanceToShaderIdMaps;
        std::vector<std::unique_ptr<Buffer>> mWorkListCounters;
        std::vector<std::unique_ptr<Buffer>> mWorkLists;
        std::vector<std::unique_ptr<Buffer>> mShaderIdToTileCounts;
        std::vector<std::unique_ptr<Buffer>> mOffsetTables;
        std::vector<std::unique_ptr<Buffer>> mTileBuffers;
        std::vector<std::unique_ptr<DescriptorSet>> mWorkListDescSets;
        uint32_t mCurrentInstance{1};

        uint32_t *mInstanceToShaderIdMapPtr{nullptr};
    };
} // namespace im