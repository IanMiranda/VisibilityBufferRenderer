#include "VisibilityBufferBackend.h"

#include "API/Buffer.h"
#include "API/CommandBuffer.h"
#include "API/DescriptorSet.h"
#include "API/PipelineLayout.h"
#include "API/RenderPass.h"
#include "API/Shader.h"
#include "API/Swapchain.h"
#include "Common.h"
#include "Renderer.h"
#include "Scene.h"
#include "glm/gtc/type_ptr.hpp"
#include "vulkan/vulkan_core.h"
#include <memory>

namespace im
{
    VisibilityBufferBackend::VisibilityBufferBackend(Renderer &renderer,
                                                     size_t maxFramesInFlight,
                                                     Image &depthImage)
        : mSetAllocator(renderer.GetDevice()),
          mVisPipeLayout(renderer.GetDevice(), {},
                         {PushConstantRange(VK_SHADER_STAGE_VERTEX_BIT,
                                            sizeof(VbPassData), 0)}),
          mVisPipe(
              renderer.GetDevice(),
              GraphicsPipelineDesc(
                  mVisPipeLayout,
                  Shader(renderer.GetDevice(),
                         "./Assets/Shaders/Bin/VisibilityPass.spv")
                      .AddStage(VK_SHADER_STAGE_VERTEX_BIT, "VSVisibilityPass")
                      .AddStage(VK_SHADER_STAGE_FRAGMENT_BIT,
                                "FSVisibilityPass"),
                  {}, InputAssembly(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST),
                  Rasterizer(VK_CULL_MODE_BACK_BIT,
                             VK_FRONT_FACE_COUNTER_CLOCKWISE,
                             VK_POLYGON_MODE_FILL),
                  Multisample(VK_SAMPLE_COUNT_1_BIT),
                  {ColorAttachment(VK_FORMAT_R32G32_UINT)},
                  {DepthStencil(depthImage.GetFormat())})),
          mBindlessSet(renderer.GetDevice()),
          mWorkListDsl(
              renderer.GetDevice(),
              {DescriptorSetLayout::Binding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                                            VK_SHADER_STAGE_COMPUTE_BIT)}),
          mWorkListPipeLayout(renderer.GetDevice(), {std::ref(mWorkListDsl)},
                              {PushConstantRange(VK_SHADER_STAGE_COMPUTE_BIT,
                                                 sizeof(VbBuildData), 0)}),
          mWorkListPipe(
              renderer.GetDevice(),
              ComputePipelineDesc(
                  mWorkListPipeLayout,
                  Shader(renderer.GetDevice(),
                         "./Assets/Shaders/Bin/VisibilityWorklist.spv")
                      .AddStage(VK_SHADER_STAGE_COMPUTE_BIT,
                                "CSBuildWorklist"))),
          mSortDsl(
              renderer.GetDevice(),
              {DescriptorSetLayout::Binding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                                            VK_SHADER_STAGE_COMPUTE_BIT)}),
          mSortPipeLayout(renderer.GetDevice(), {std::ref(mSortDsl)},
                          {PushConstantRange(VK_SHADER_STAGE_COMPUTE_BIT,
                                             sizeof(VbSortData), 0)}),
          mSortPipe(renderer.GetDevice(),
                    ComputePipelineDesc(
                        mSortPipeLayout,
                        Shader(renderer.GetDevice(),
                               "./Assets/Shaders/Bin/VisibilitySort.spv")
                            .AddStage(VK_SHADER_STAGE_COMPUTE_BIT,
                                      "CSSortWorkList"))),
          mShadeDsl(
              renderer.GetDevice(),
              {DescriptorSetLayout::Binding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                                            VK_SHADER_STAGE_COMPUTE_BIT)}),
          mShadePipeLayout(
              renderer.GetDevice(),
              {std::ref(mShadeDsl), std::ref(mBindlessSet.GetSetLayout())},
              {PushConstantRange(VK_SHADER_STAGE_COMPUTE_BIT,
                                 sizeof(VbShadingData))}),
          mShadePipe(renderer.GetDevice(),
                     ComputePipelineDesc(
                         mShadePipeLayout,
                         Shader(renderer.GetDevice(),
                                "./Assets/Shaders/Bin/VisibilityShading.spv")
                             .AddStage(VK_SHADER_STAGE_COMPUTE_BIT,
                                       "CSVisibilityShading"))),
          mVisBuffers(InitVisBuffers(renderer, maxFramesInFlight)),
          mInstanceToShaderIdMaps(InitBuffers(
              renderer, maxFramesInFlight,
              BufferDesc(
                  sizeof(uint32_t) * MaxDrawCalls,
                  VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT |
                      VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT,
                  VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT))),
          mWorkListCounters(InitBuffers(
              renderer, maxFramesInFlight,
              BufferDesc(sizeof(uint32_t),
                         VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                             VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT |
                             VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT))),
          mWorkLists(InitBuffers(
              renderer, maxFramesInFlight,
              BufferDesc(
                  MaxShaders *
                      GetTileCount(
                          renderer.GetDevice().GetSwapchain().GetExtent()) *
                      sizeof(VbWorkItem),
                  VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT |
                      VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT))),
          mShaderIdToTileCounts(InitBuffers(
              renderer, maxFramesInFlight,
              BufferDesc(MaxShaders * sizeof(uint32_t),
                         VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                             VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT |
                             VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT))),
          mOffsetTables(InitBuffers(
              renderer, maxFramesInFlight,
              BufferDesc((MaxShaders + 1) * sizeof(uint32_t),
                         VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT |
                             VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT))),
          mTileBuffers(InitBuffers(
              renderer, maxFramesInFlight,
              BufferDesc(
                  MaxShaders *
                      GetTileCount(
                          renderer.GetDevice().GetSwapchain().GetExtent()) *
                      sizeof(VbWorkItem),
                  VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT |
                      VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT))),
          mVertexBuffers(InitBuffers(
              renderer, maxFramesInFlight,
              BufferDesc(
                  sizeof(VkDeviceAddress) * MaxDrawCalls,
                  VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT |
                      VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT,
                  VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT))),
          mIndexBuffers(InitBuffers(
              renderer, maxFramesInFlight,
              BufferDesc(
                  sizeof(VkDeviceAddress) * MaxDrawCalls,
                  VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT |
                      VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT,
                  VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT))),
          mTransformBuffers(InitBuffers(
              renderer, maxFramesInFlight,
              BufferDesc(
                  sizeof(glm::mat4) * MaxDrawCalls,
                  VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT |
                      VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT,
                  VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT))),
          mMaterialBuffers(InitBuffers(
              renderer, maxFramesInFlight,
              BufferDesc(
                  sizeof(VbMaterialData) * MaxDrawCalls,
                  VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT |
                      VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT,
                  VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT))),
          mIndirectBuffers(InitBuffers(
              renderer, maxFramesInFlight,
              BufferDesc(
                  sizeof(VbDispatchIndirectCommand) * (MaxShaders + 1),
                  VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT |
                      VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT |
                      VK_BUFFER_USAGE_2_INDIRECT_BUFFER_BIT,
                  VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT))),
          mWorkListDescSets(InitDescSets(renderer, maxFramesInFlight))
    {
    }

    void VisibilityBufferBackend::BeginScene(CommandBuffer &cmd,
                                             ImageView &depthView,
                                             uint32_t frameIndex)
    {
        cmd.SetViewportAndScissor(mVisBuffers[frameIndex].image->GetExtent());

        cmd.Barrier(
            {},
            {ImageMemoryBarrier(*mVisBuffers[frameIndex].image,
                                VK_IMAGE_LAYOUT_UNDEFINED,
                                VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE,
                                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                                VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                                VK_IMAGE_ASPECT_COLOR_BIT)},
            {BufferMemoryBarrier(*mWorkListCounters[frameIndex],
                                 VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE,
                                 VK_PIPELINE_STAGE_2_CLEAR_BIT,
                                 VK_ACCESS_2_TRANSFER_WRITE_BIT),
             BufferMemoryBarrier(*mWorkLists[frameIndex],
                                 VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE,
                                 VK_PIPELINE_STAGE_2_CLEAR_BIT,
                                 VK_ACCESS_2_TRANSFER_WRITE_BIT),
             BufferMemoryBarrier(*mShaderIdToTileCounts[frameIndex],
                                 VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE,
                                 VK_PIPELINE_STAGE_2_CLEAR_BIT,
                                 VK_ACCESS_2_TRANSFER_WRITE_BIT)});

        vkCmdFillBuffer(cmd.Get(), mWorkListCounters[frameIndex]->Get(), 0,
                        VK_WHOLE_SIZE, 0);
        vkCmdFillBuffer(cmd.Get(), mWorkLists[frameIndex]->Get(), 0,
                        VK_WHOLE_SIZE, 0); // Not needed, but nice for debugging
        vkCmdFillBuffer(cmd.Get(), mShaderIdToTileCounts[frameIndex]->Get(), 0,
                        VK_WHOLE_SIZE, 0);

        cmd.Barrier(
            {}, {},
            {BufferMemoryBarrier(*mWorkListCounters[frameIndex],
                                 VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE,
                                 VK_PIPELINE_STAGE_2_CLEAR_BIT,
                                 VK_ACCESS_2_TRANSFER_WRITE_BIT),
             BufferMemoryBarrier(*mWorkLists[frameIndex],
                                 VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE,
                                 VK_PIPELINE_STAGE_2_CLEAR_BIT,
                                 VK_ACCESS_2_TRANSFER_WRITE_BIT),
             BufferMemoryBarrier(*mShaderIdToTileCounts[frameIndex],
                                 VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE,
                                 VK_PIPELINE_STAGE_2_CLEAR_BIT,
                                 VK_ACCESS_2_TRANSFER_WRITE_BIT)});

        cmd.BeginRendering({ColorAttachment(mVisBuffers[frameIndex].view->Get(),
                                            VK_ATTACHMENT_LOAD_OP_CLEAR,
                                            VK_ATTACHMENT_STORE_OP_STORE)},
                           DepthAttachment(depthView.Get(),
                                           VK_ATTACHMENT_LOAD_OP_CLEAR,
                                           VK_ATTACHMENT_STORE_OP_DONT_CARE),
                           Scissor(mVisBuffers[frameIndex].image->GetExtent()));

        cmd.BindGraphicsPipeline(mVisPipe);

        mInstanceToShaderIdMapPtr = reinterpret_cast<uint32_t *>(
            mInstanceToShaderIdMaps[frameIndex]->Map());
        mVertexBuffersPtr = reinterpret_cast<VkDeviceAddress *>(
            mVertexBuffers[frameIndex]->Map());
        mIndexBuffersPtr = reinterpret_cast<VkDeviceAddress *>(
            mIndexBuffers[frameIndex]->Map());
        mTransformBufferPtr =
            reinterpret_cast<float *>(mTransformBuffers[frameIndex]->Map());
        mMaterialBufferPtr = reinterpret_cast<VbMaterialData *>(
            mMaterialBuffers[frameIndex]->Map());

        *mInstanceToShaderIdMapPtr = 0;
        ++mInstanceToShaderIdMapPtr;
        ++mVertexBuffersPtr;
        ++mIndexBuffersPtr;
        mTransformBufferPtr += sizeof(glm::mat4);
        ++mMaterialBufferPtr;
    }

    void VisibilityBufferBackend::DrawBatch(
        Scene &scene, CommandBuffer &cmd, const std::vector<VbObject> &objects)
    {
        const auto &camera = scene.GetCamera();
        const auto viewProj =
            camera.GetProjectionMatrix() * camera.GetViewMatrix();
        VbPassData passData{};

        for (const auto &object : objects)
        {
            const auto vboAddress = object.mesh->vertexBuffer->GetAddress();

            passData.modelViewProj = viewProj * object.transform;
            passData.vertexData = vboAddress;

            cmd.PushConstants(mVisPipeLayout, VK_SHADER_STAGE_VERTEX_BIT,
                              passData);
            cmd.BindIndexBuffer(*object.mesh->indexBuffer);
            cmd.DrawIndexed(object.mesh->indexCount, 1, 0, 0, mCurrentInstance);
            *mInstanceToShaderIdMapPtr = 1; // TODO: support multiple materials?
            ++mInstanceToShaderIdMapPtr;

            *mVertexBuffersPtr = vboAddress;
            ++mVertexBuffersPtr;

            *mIndexBuffersPtr = object.mesh->indexBuffer->GetAddress();
            ++mIndexBuffersPtr;

            std::memcpy(mTransformBufferPtr,
                        glm::value_ptr(passData.modelViewProj),
                        sizeof(passData.modelViewProj));
            ++mTransformBufferPtr;

            VbMaterialData material{};
            material.albedoMapIndex =
                mBindlessSet.GetOrCreateId(object.material.albedoMap);

            std::memcpy(mMaterialBufferPtr, &material, sizeof(material));
            ++mMaterialBufferPtr;

            ++mCurrentInstance;
        }
    }

    void VisibilityBufferBackend::End(Renderer &renderer, CommandBuffer &cmd,
                                      uint32_t frameIndex)
    {
        cmd.EndRendering();

        cmd.Barrier(
            {},
            {ImageMemoryBarrier(
                *mVisBuffers[frameIndex].image,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_READ_BIT, VK_IMAGE_ASPECT_COLOR_BIT)},
            {});

        cmd.Barrier(
            {MemoryBarrier(
                VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
                VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT)},
            {}, {});

        mInstanceToShaderIdMaps[frameIndex]->Unmap();
        mInstanceToShaderIdMapPtr = nullptr;

        mVertexBuffers[frameIndex]->Unmap();
        mVertexBuffersPtr = nullptr;

        mIndexBuffers[frameIndex]->Unmap();
        mIndexBuffersPtr = nullptr;

        mTransformBuffers[frameIndex]->Unmap();
        mTransformBufferPtr = nullptr;

        mMaterialBuffers[frameIndex]->Unmap();
        mMaterialBufferPtr = nullptr;

        // Now, build the worklist and sort it
        VbBuildData buildData{};
        buildData.windowSize =
            glm::uvec2(renderer.GetDevice().GetSwapchain().GetExtent().width,
                       renderer.GetDevice().GetSwapchain().GetExtent().height);
        buildData.instanceToShaderIdMap =
            mInstanceToShaderIdMaps[frameIndex]->GetAddress();
        buildData.workListCounter = mWorkListCounters[frameIndex]->GetAddress();
        buildData.workList = mWorkLists[frameIndex]->GetAddress();
        buildData.shaderIdToTileCount =
            mShaderIdToTileCounts[frameIndex]->GetAddress();

        cmd.BindComputePipeline(mWorkListPipe);
        cmd.BindComputeDescriptorSets(
            mWorkListPipeLayout, 0, {std::ref(*mWorkListDescSets[frameIndex])});
        cmd.PushConstants(mWorkListPipeLayout, VK_SHADER_STAGE_COMPUTE_BIT,
                          buildData);
        cmd.Dispatch((buildData.windowSize.x + TileSize.x - 1) /
                         TileSize.x, // Ceiling
                     (buildData.windowSize.y + TileSize.y - 1) / TileSize.y, 1);

        // Sort pass
        cmd.Barrier({}, {},
                    {BufferMemoryBarrier(*mWorkListCounters[frameIndex],
                                         VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                                         VK_ACCESS_2_SHADER_WRITE_BIT,
                                         VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                                         VK_ACCESS_2_SHADER_READ_BIT),
                     BufferMemoryBarrier(*mWorkLists[frameIndex],
                                         VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                                         VK_ACCESS_2_SHADER_WRITE_BIT,
                                         VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                                         VK_ACCESS_2_SHADER_READ_BIT),
                     BufferMemoryBarrier(*mShaderIdToTileCounts[frameIndex],
                                         VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                                         VK_ACCESS_2_SHADER_WRITE_BIT,
                                         VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                                         VK_ACCESS_2_SHADER_READ_BIT)});

        cmd.Barrier(
            {MemoryBarrier(
                VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
                VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT)},
            {}, {});

        VbSortData sortData{};
        sortData.worklistCounter = mWorkListCounters[frameIndex]->GetAddress();
        sortData.workList = mWorkLists[frameIndex]->GetAddress();
        sortData.shaderIdToTileCount =
            mShaderIdToTileCounts[frameIndex]->GetAddress();
        sortData.offsetTable = mOffsetTables[frameIndex]->GetAddress();
        sortData.tileBuffer = mTileBuffers[frameIndex]->GetAddress();
        sortData.indirectBuffer = mIndirectBuffers[frameIndex]->GetAddress();
        sortData.windowSize = buildData.windowSize;

        cmd.BindComputePipeline(mSortPipe);
        cmd.PushConstants(mSortPipeLayout, VK_SHADER_STAGE_COMPUTE_BIT,
                          sortData);

        cmd.Dispatch(1, 1, 1);

        cmd.Barrier(
            {MemoryBarrier(
                VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
                VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT)},
            {}, {});

        // Finally, the shading pass
        VbShadingData shadingData{};
        shadingData.instanceToShaderIdMap =
            mInstanceToShaderIdMaps[frameIndex]->GetAddress();
        shadingData.vertexBuffers = mVertexBuffers[frameIndex]->GetAddress();
        shadingData.indexBuffers = mIndexBuffers[frameIndex]->GetAddress();
        shadingData.transforms = mTransformBuffers[frameIndex]->GetAddress();
        shadingData.offsetTable = mOffsetTables[frameIndex]->GetAddress();
        shadingData.tiles = mTileBuffers[frameIndex]->GetAddress();
        shadingData.materials = mMaterialBuffers[frameIndex]->GetAddress();
        shadingData.shaderId = 1;

        cmd.BindComputePipeline(mShadePipe);
        cmd.BindComputeDescriptorSets(mShadePipeLayout, 0,
                                      {std::ref(*mWorkListDescSets[frameIndex]),
                                       std::ref(mBindlessSet.Get())});
        cmd.PushConstants(mShadePipeLayout, VK_SHADER_STAGE_COMPUTE_BIT,
                          shadingData);

        vkCmdDispatchIndirect(cmd.Get(), mIndirectBuffers[frameIndex]->Get(),
                              0 * sizeof(VbDispatchIndirectCommand));

        mCurrentInstance = 1;
    }

    void VisibilityBufferBackend::ResizeBuffers(Renderer &renderer,
                                                size_t maxFramesInFlight)
    {
        mVisBuffers = InitVisBuffers(renderer, maxFramesInFlight);
        mWorkLists = InitBuffers(
            renderer, maxFramesInFlight,
            BufferDesc(MaxShaders * sizeof(VbWorkItem) *
                           GetTileCount(
                               renderer.GetDevice().GetSwapchain().GetExtent()),
                       VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT |
                           VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT));

        mTileBuffers = InitBuffers(
            renderer, maxFramesInFlight,
            BufferDesc(
                MaxShaders *
                    GetTileCount(
                        renderer.GetDevice().GetSwapchain().GetExtent()) *
                    sizeof(VbWorkItem),
                VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT |
                    VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT));
    }

    std::vector<Texture2D> VisibilityBufferBackend::InitVisBuffers(
        Renderer &renderer, size_t count)
    {
        std::vector<Texture2D> res;
        res.reserve(count);

        const auto extent = renderer.GetDevice().GetSwapchain().GetExtent();

        for (size_t i = 0; i < count; ++i)
        {
            Texture2D visBuf{};
            visBuf.image = std::make_unique<Image>(
                renderer.GetDevice(), VK_FORMAT_R32G32_UINT,
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                    VK_IMAGE_USAGE_STORAGE_BIT,
                extent.width, extent.height, 1, 1, VK_IMAGE_TYPE_2D, 1, 0);
            visBuf.view = std::make_unique<ImageView>(
                renderer.GetDevice(), *visBuf.image, VK_IMAGE_VIEW_TYPE_2D,
                VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1);
            res.emplace_back(std::move(visBuf));
        }

        return res;
    }

    std::vector<std::unique_ptr<Buffer>> VisibilityBufferBackend::InitBuffers(
        Renderer &renderer, size_t count, const BufferDesc &desc)
    {
        std::vector<std::unique_ptr<Buffer>> res;
        res.reserve(count);

        for (size_t i = 0; i < count; ++i)
        {
            res.emplace_back(
                std::make_unique<Buffer>(renderer.GetDevice(), desc));
        }

        return res;
    }

    std::vector<std::unique_ptr<DescriptorSet>> VisibilityBufferBackend::
        InitDescSets(Renderer &renderer, size_t count)
    {
        std::vector<std::unique_ptr<DescriptorSet>> res;
        res.reserve(count);

        for (size_t i = 0; i < count; ++i)
        {
            auto set = mSetAllocator.Allocate(mWorkListDsl);
            set->PushWrite(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                           *mVisBuffers[i].view, VK_IMAGE_LAYOUT_GENERAL)
                .Update();
            res.emplace_back(std::move(set));
        }

        return res;
    }

    uint32_t VisibilityBufferBackend::GetTileCount(VkExtent2D extent)
    {
        return ((extent.width + TileSize.x - 1) / TileSize.x) *
               ((extent.height + TileSize.y - 1) / TileSize.y);
    }
} // namespace im