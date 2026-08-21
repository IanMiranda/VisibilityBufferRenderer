#include "VisibilityBufferBackend.h"

#include "API/Buffer.h"
#include "API/CommandBuffer.h"
#include "API/DescriptorSet.h"
#include "API/Device.h"
#include "API/PipelineLayout.h"
#include "API/RenderPass.h"
#include "API/Shader.h"
#include "API/Swapchain.h"
#include "Common.h"
#include "Renderer.h"
#include "Scene.h"
#include "glm/gtc/type_ptr.hpp"
#include "stb_image.h"
#include "vulkan/vulkan_core.h"
#include <glm/gtx/string_cast.hpp>
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
                                            VK_SHADER_STAGE_COMPUTE_BIT),
               DescriptorSetLayout::Binding(1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                                            VK_SHADER_STAGE_COMPUTE_BIT),
               DescriptorSetLayout::Binding(2,
                                            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                            VK_SHADER_STAGE_COMPUTE_BIT)},
              VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT),
          mShadeLightDsl(renderer.GetDevice(),
                         {DescriptorSetLayout::Binding(
                              0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                              VK_SHADER_STAGE_COMPUTE_BIT),
                          DescriptorSetLayout::Binding(
                              1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                              VK_SHADER_STAGE_COMPUTE_BIT),
                          DescriptorSetLayout::Binding(
                              2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                              VK_SHADER_STAGE_COMPUTE_BIT),
                          DescriptorSetLayout::Binding(
                              3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                              VK_SHADER_STAGE_COMPUTE_BIT)}),
          mShadePipeLayout(renderer.GetDevice(),
                           {std::ref(mShadeDsl), std::ref(mShadeLightDsl),
                            std::ref(mBindlessSet.GetSetLayout())},
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
                  VK_BUFFER_USAGE_TRANSFER_DST_BIT |
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
          mShadeConstants(InitBuffers(
              renderer, maxFramesInFlight,
              BufferDesc(
                  sizeof(VbConstantData), VK_BUFFER_USAGE_2_UNIFORM_BUFFER_BIT,
                  VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT))),
          mLightData(InitBuffers(
              renderer, maxFramesInFlight,
              BufferDesc(
                  sizeof(VbLightData), VK_BUFFER_USAGE_2_UNIFORM_BUFFER_BIT,
                  VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT))),

          mWorkListDescSets(InitDescSets(renderer, maxFramesInFlight))
    {
        stbi_set_flip_vertically_on_load(true);
        int width, height, channels;
        float *data = stbi_loadf("./Assets/Textures/stadium_exterior_4k.hdr",
                                 &width, &height, &channels, STBI_rgb_alpha);
        if (!data)
        {
            fmt::println(stderr, "Failed to load HDR environment map!");
            return;
        }

        auto eqMapImage = std::make_unique<Image>(
            renderer.GetDevice(), VK_FORMAT_R32G32B32A32_SFLOAT,
            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, width,
            height, 1, 1, VK_IMAGE_TYPE_2D, 1);
        auto eqMapView = std::make_unique<ImageView>(
            renderer.GetDevice(), *eqMapImage, VK_IMAGE_VIEW_TYPE_2D,
            VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1);

        Texture2D equirectangularMap = {std::move(eqMapImage),
                                        std::move(eqMapView)};
        Buffer staging(renderer.GetDevice(),
                       BufferDesc::Upload(width * height * 4 * sizeof(float)),
                       data);
        renderer.GetDevice().RunImmediateCommands([&staging,
                                                   &equirectangularMap,
                                                   this](CommandBuffer &cmds) {
            cmds.Barrier(
                {},
                {ImageMemoryBarrier(
                    *equirectangularMap.image, VK_IMAGE_LAYOUT_UNDEFINED,
                    VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                    VK_ACCESS_2_TRANSFER_WRITE_BIT,
                    equirectangularMap.view->GetAspect(), 0, 1, 0, 1)},
                {});
            cmds.Copy(staging, *equirectangularMap.image,
                      equirectangularMap.view->GetAspect(), 0, 1, 0);
            cmds.Barrier({},
                         {ImageMemoryBarrier(
                             *equirectangularMap.image,
                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                             VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                             VK_ACCESS_2_TRANSFER_WRITE_BIT,
                             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                             VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                             VK_ACCESS_2_SHADER_READ_BIT,
                             equirectangularMap.view->GetAspect(), 0, 1, 0, 1)},
                         {});
        });

        stbi_image_free(data);

        mEnvMap = EquirectangularToCubemap(renderer, depthImage,
                                           *equirectangularMap.view);
        mIrradianceMap =
            CalculateDiffuseIrradiance(renderer, depthImage, *mEnvMap.view);
        mPrefilteredEnvMap =
            PrefilterEnvMap(renderer, depthImage, *mEnvMap.view);
        mBrdfLut = GenerateBrdfLut(renderer, depthImage);

        mLightDescSets.reserve(maxFramesInFlight);
        for (size_t i = 0; i < maxFramesInFlight; ++i)
        {
            auto set = mSetAllocator.Allocate(mShadeLightDsl);
            set->PushWrite(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, *mLightData[i])
                .PushWrite(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                           *mIrradianceMap.view,
                           renderer.GetDevice().GetSamplers().TrilinearColor())
                .PushWrite(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                           *mPrefilteredEnvMap.view,
                           renderer.GetDevice().GetSamplers().TrilinearColor())
                .PushWrite(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                           *mBrdfLut.view,
                           renderer.GetDevice().GetSamplers().TrilinearColor())
                .Update();
            mLightDescSets.emplace_back(std::move(set));
        }
    }

    void VisibilityBufferBackend::BeginScene(Scene &scene, CommandBuffer &cmd,
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
                                VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT |
                                    VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT,
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
                        VK_WHOLE_SIZE,
                        0); // Not needed, but nice for debugging
        vkCmdFillBuffer(cmd.Get(), mShaderIdToTileCounts[frameIndex]->Get(), 0,
                        VK_WHOLE_SIZE, 0);

        cmd.Barrier(
            {}, {},
            {BufferMemoryBarrier(
                 *mWorkListCounters[frameIndex], VK_PIPELINE_STAGE_2_CLEAR_BIT,
                 VK_ACCESS_2_TRANSFER_WRITE_BIT,
                 VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                 VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT),
             BufferMemoryBarrier(
                 *mWorkLists[frameIndex], VK_PIPELINE_STAGE_2_CLEAR_BIT,
                 VK_ACCESS_2_TRANSFER_WRITE_BIT,
                 VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                 VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT),
             BufferMemoryBarrier(
                 *mShaderIdToTileCounts[frameIndex],
                 VK_PIPELINE_STAGE_2_CLEAR_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                 VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                 VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT)});

        cmd.BeginRendering({ColorAttachment(mVisBuffers[frameIndex].view->Get(),
                                            VK_ATTACHMENT_LOAD_OP_CLEAR,
                                            VK_ATTACHMENT_STORE_OP_STORE)},
                           DepthAttachment(depthView.Get(),
                                           VK_ATTACHMENT_LOAD_OP_CLEAR,
                                           VK_ATTACHMENT_STORE_OP_STORE),
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
        mTransformBufferPtr += 16;
        ++mMaterialBufferPtr;

        auto lightData =
            reinterpret_cast<VbLightData *>(mLightData[frameIndex]->Map());
        for (size_t i = 0; i < scene.GetPointLights().size(); ++i)
        {
            lightData->lights[i] = scene.GetPointLights()[i];
        }
        lightData->lightCount = scene.GetPointLights().size();
        mLightData[frameIndex]->Unmap();
    }

    void VisibilityBufferBackend::DrawBatch(
        Scene &scene, CommandBuffer &cmd, const std::vector<VbObject> &objects,
        uint32_t frameIndex)
    {
        const auto &camera = scene.GetCamera();
        const auto viewProj =
            camera.GetProjectionMatrix() * camera.GetViewMatrix();
        VbPassData passData{};

        // TODO: move this out of the loop
        auto constants = reinterpret_cast<VbConstantData *>(
            mShadeConstants[frameIndex]->Map());
        constants->view = camera.GetViewMatrix();
        constants->viewProj = viewProj;
        constants->viewInverse = glm::inverse(constants->view);
        mShadeConstants[frameIndex]->Unmap();

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

            *mVertexBuffersPtr = object.mesh->vertexBuffer->GetAddress();
            ++mVertexBuffersPtr;

            *mIndexBuffersPtr = object.mesh->indexBuffer->GetAddress();
            ++mIndexBuffersPtr;

            std::memcpy(mTransformBufferPtr,
                        glm::value_ptr(passData.modelViewProj),
                        sizeof(passData.modelViewProj));
            mTransformBufferPtr += 16;

            VbMaterialData material{};
            material.albedoMapIndex =
                mBindlessSet.GetOrCreateId(object.material.albedoMap);
            material.metallicMapIndex =
                mBindlessSet.GetOrCreateId(object.material.metallicMap);
            material.roughnessMapIndex =
                mBindlessSet.GetOrCreateId(object.material.roughnessMap);
            material.normalMapIndex =
                mBindlessSet.GetOrCreateId(object.material.normalMap);
            material.aoMapIndex =
                mBindlessSet.GetOrCreateId(object.material.aoMap);
            material.emissiveMapIndex =
                mBindlessSet.GetOrCreateId(object.material.emissiveMap);

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
                VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT |
                    VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT,
                VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_READ_BIT, VK_IMAGE_ASPECT_COLOR_BIT)},
            {});

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
        cmd.Barrier(
            {}, {},
            {BufferMemoryBarrier(
                 *mWorkListCounters[frameIndex],
                 VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                 VK_ACCESS_2_SHADER_WRITE_BIT | VK_ACCESS_2_SHADER_READ_BIT,
                 VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                 VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT),
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

        // Finally, the shading pass

        cmd.Barrier(
            {},
            {ImageMemoryBarrier(
                renderer.GetDevice().GetSwapchain().GetImages()
                    [renderer.GetDevice().GetSwapchain().GetImageIndex()],
                VK_IMAGE_LAYOUT_UNDEFINED,
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_PIPELINE_STAGE_2_CLEAR_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                VK_IMAGE_ASPECT_COLOR_BIT)},
            {BufferMemoryBarrier(*mOffsetTables[frameIndex],
                                 VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                                 VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT |
                                     VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
                                 VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                                 VK_ACCESS_2_SHADER_STORAGE_READ_BIT |
                                     VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT),
             BufferMemoryBarrier(*mTileBuffers[frameIndex],
                                 VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                                 VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT |
                                     VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
                                 VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                                 VK_ACCESS_2_SHADER_STORAGE_READ_BIT |
                                     VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT),
             BufferMemoryBarrier(*mIndirectBuffers[frameIndex],
                                 VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                                 VK_ACCESS_2_SHADER_WRITE_BIT,
                                 VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT,
                                 VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT)});

        // TODO: maybe just transfer DST?
        VkClearColorValue color = {0.0f, 0.0f, 0.0f, 1.0f};
        VkImageSubresourceRange subres{};
        subres.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        subres.baseArrayLayer = 0;
        subres.layerCount = 1;
        subres.baseMipLevel = 0;
        subres.levelCount = 1;

        vkCmdClearColorImage(
            cmd.Get(),
            renderer.GetDevice().GetSwapchain().GetImages()
                [renderer.GetDevice().GetSwapchain().GetImageIndex()],
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &color, 1, &subres);

        cmd.Barrier(
            {},
            {ImageMemoryBarrier(
                renderer.GetDevice().GetSwapchain().GetImages()
                    [renderer.GetDevice().GetSwapchain().GetImageIndex()],
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_PIPELINE_STAGE_2_CLEAR_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT |
                    VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
                VK_IMAGE_ASPECT_COLOR_BIT)},
            {});

        VbShadingData shadingData{};
        shadingData.instanceToShaderIdMap =
            mInstanceToShaderIdMaps[frameIndex]->GetAddress();
        shadingData.vertexBuffers = mVertexBuffers[frameIndex]->GetAddress();
        shadingData.indexBuffers = mIndexBuffers[frameIndex]->GetAddress();
        shadingData.transforms = mTransformBuffers[frameIndex]->GetAddress();
        shadingData.offsetTable = mOffsetTables[frameIndex]->GetAddress();
        shadingData.tiles = mTileBuffers[frameIndex]->GetAddress();
        shadingData.materials = mMaterialBuffers[frameIndex]->GetAddress();
        shadingData.screenSize = buildData.windowSize;
        shadingData.shaderId = 1;

        cmd.BindComputePipeline(mShadePipe);

        {
            VkDescriptorImageInfo visBufDesc{};
            visBufDesc.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            visBufDesc.imageView = mVisBuffers[frameIndex].view->Get();

            VkDescriptorImageInfo frameBufDesc{};
            frameBufDesc.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            frameBufDesc.imageView =
                renderer.GetDevice().GetSwapchain().GetViews()
                    [renderer.GetDevice().GetSwapchain().GetImageIndex()];

            VkDescriptorBufferInfo constantDesc{};
            constantDesc.buffer = mShadeConstants[frameIndex]->Get();
            constantDesc.offset = 0;
            constantDesc.range = VK_WHOLE_SIZE;

            std::array<VkWriteDescriptorSet, 3> bufferWrites{};
            bufferWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            bufferWrites[0].pImageInfo = &visBufDesc;
            bufferWrites[0].pTexelBufferView = nullptr;
            bufferWrites[0].pBufferInfo = nullptr;
            bufferWrites[0].descriptorCount = 1;
            bufferWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            bufferWrites[0].dstArrayElement = 0;
            bufferWrites[0].dstBinding = 0;
            bufferWrites[0].pNext = nullptr;
            bufferWrites[0].dstSet = 0;

            bufferWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            bufferWrites[1].pImageInfo = &frameBufDesc;
            bufferWrites[1].pTexelBufferView = nullptr;
            bufferWrites[1].pBufferInfo = nullptr;
            bufferWrites[1].descriptorCount = 1;
            bufferWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            bufferWrites[1].dstArrayElement = 0;
            bufferWrites[1].dstBinding = 1;
            bufferWrites[1].pNext = nullptr;
            bufferWrites[1].dstSet = 0;

            bufferWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            bufferWrites[2].pImageInfo = nullptr;
            bufferWrites[2].pTexelBufferView = nullptr;
            bufferWrites[2].pBufferInfo = &constantDesc;
            bufferWrites[2].descriptorCount = 1;
            bufferWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            bufferWrites[2].dstArrayElement = 0;
            bufferWrites[2].dstBinding = 2;
            bufferWrites[2].pNext = nullptr;
            bufferWrites[2].dstSet = 0;

            vkCmdPushDescriptorSet(cmd.Get(), VK_PIPELINE_BIND_POINT_COMPUTE,
                                   mShadePipeLayout.Get(), 0,
                                   bufferWrites.size(), bufferWrites.data());
        }

        cmd.BindComputeDescriptorSets(mShadePipeLayout, 1,
                                      {std::ref(*mLightDescSets[frameIndex]),
                                       std::ref(mBindlessSet.Get())});
        cmd.PushConstants(mShadePipeLayout, VK_SHADER_STAGE_COMPUTE_BIT,
                          shadingData);

        // TODO: add support for more shaders
        for (size_t shader = 0; shader < 2; ++shader)
        {
            vkCmdDispatchIndirect(cmd.Get(),
                                  mIndirectBuffers[frameIndex]->Get(),
                                  shader * sizeof(VbDispatchIndirectCommand));
            cmd.Barrier(
                {},
                {ImageMemoryBarrier(
                    renderer.GetDevice().GetSwapchain().GetImages()
                        [renderer.GetDevice().GetSwapchain().GetImageIndex()],
                    VK_IMAGE_LAYOUT_GENERAL,
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                    VK_ACCESS_2_SHADER_STORAGE_READ_BIT |
                        VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                    VK_IMAGE_LAYOUT_GENERAL,
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                    VK_ACCESS_2_SHADER_STORAGE_READ_BIT |
                        VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                    VK_IMAGE_ASPECT_COLOR_BIT)},
                {});
        }

        cmd.Barrier(
            {},
            {ImageMemoryBarrier(
                renderer.GetDevice().GetSwapchain().GetImages()
                    [renderer.GetDevice().GetSwapchain().GetImageIndex()],
                VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_READ_BIT |
                    VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT |
                    VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                VK_IMAGE_ASPECT_COLOR_BIT)},
            {});

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

            renderer.GetDevice().SetDebugName(
                VK_OBJECT_TYPE_IMAGE, (uint64_t)visBuf.image->Get(),
                fmt::format("Visibility Image {}", i));

            renderer.GetDevice().SetDebugName(
                VK_OBJECT_TYPE_IMAGE_VIEW, (uint64_t)visBuf.view->Get(),
                fmt::format("Visibility ImageView {}", i));

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

    TextureCube VisibilityBufferBackend::EquirectangularToCubemap(
        Renderer &renderer, Image &depthImage, ImageView &eqMap)
    {
        auto cubeVertexBuffer = CreateCubeVertexBuffer(renderer);

        DescriptorSetLayout cubeSetLayout(
            renderer.GetDevice(),
            {DescriptorSetLayout::Binding(
                0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                VK_SHADER_STAGE_FRAGMENT_BIT, 1)});
        PipelineLayout cubePipeLayout(
            renderer.GetDevice(), {std::ref(cubeSetLayout)},
            {PushConstantRange(VK_SHADER_STAGE_VERTEX_BIT, sizeof(EqMapData))});

        GraphicsPipeline cubePipe(
            renderer.GetDevice(),
            GraphicsPipelineDesc(
                cubePipeLayout,
                Shader(renderer.GetDevice(),
                       "./Assets/Shaders/Bin/EquirectangularToCubemap.spv")
                    .AddStage(VK_SHADER_STAGE_VERTEX_BIT, "VSMain")
                    .AddStage(VK_SHADER_STAGE_FRAGMENT_BIT, "FSMain"),
                {InputBinding(
                    {InputAttribute(0, VK_FORMAT_R32G32B32_SFLOAT, 0)},
                    VK_VERTEX_INPUT_RATE_VERTEX, sizeof(float) * 3)},
                InputAssembly(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST),
                Rasterizer(VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE,
                           VK_POLYGON_MODE_FILL),
                Multisample(VK_SAMPLE_COUNT_1_BIT),
                {ColorAttachment(VK_FORMAT_R32G32B32A32_SFLOAT)},
                {DepthStencil(depthImage.GetFormat())}));

        auto cubeDescSet = mSetAllocator.Allocate(cubeSetLayout);
        cubeDescSet
            ->PushWrite(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, eqMap,
                        renderer.GetDevice().GetSamplers().TrilinearColor())
            .Update();

        auto cubeMap = std::make_unique<Image>(
            renderer.GetDevice(), VK_FORMAT_R32G32B32A32_SFLOAT,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            1024, 1024, 1, Image::CubemapFaces, VK_IMAGE_TYPE_2D, 1,
            VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT);
        auto cubeMapFaces = ImageView::CreateFacesForCubemap(
            renderer.GetDevice(), *cubeMap, VK_IMAGE_ASPECT_COLOR_BIT, 0, 1);
        std::array views{
            glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f),
                        glm::vec3(1.0f, 0.0f, 0.0f),
                        glm::vec3(0.0f, 1.0f, 0.0f)),
            glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f),
                        glm::vec3(-1.0f, 0.0f, 0.0f),
                        glm::vec3(0.0f, 1.0f, 0.0f)),
            glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f),
                        glm::vec3(0.0f, 1.0f, 0.0f),
                        glm::vec3(0.0f, 0.0f, 1.0f)),
            glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f),
                        glm::vec3(0.0f, -1.0f, 0.0f),
                        glm::vec3(0.0f, 0.0f, -1.0f)),
            glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f),
                        glm::vec3(0.0f, 0.0f, -1.0f),
                        glm::vec3(0.0f, 1.0f, 0.0f)),
            glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f),
                        glm::vec3(0.0f, 0.0f, 1.0f),
                        glm::vec3(0.0f, 1.0f, 0.0f)),
        };

        renderer.GetDevice().RunImmediateCommands([&](CommandBuffer &cmds) {
            glm::mat4 proj =
                glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 10.0f);
            cmds.Barrier(
                {},
                {ImageMemoryBarrier(
                    *cubeMap, VK_IMAGE_LAYOUT_UNDEFINED,
                    VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_ACCESS_2_NONE,
                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                    VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                    VK_IMAGE_ASPECT_COLOR_BIT, 0, Image::CubemapFaces, 0, 1)},
                {});
            cmds.SetViewportAndScissor(
                {cubeMap->GetWidth(), cubeMap->GetHeight()});
            for (uint32_t i = 0; i < Image::CubemapFaces; ++i)
            {
                cmds.BeginRendering(
                    {ColorAttachment(cubeMapFaces[i]->Get(),
                                     VK_ATTACHMENT_LOAD_OP_CLEAR,
                                     VK_ATTACHMENT_STORE_OP_STORE)},
                    Scissor({cubeMap->GetWidth(), cubeMap->GetHeight()}));
                cmds.BindGraphicsPipeline(cubePipe);
                cmds.BindGraphicsDescriptorSets(cubePipeLayout, 0,
                                                {std::ref(*cubeDescSet)});
                cmds.PushConstants(
                    cubePipeLayout, VK_SHADER_STAGE_VERTEX_BIT,
                    EqMapData(proj * glm::mat4(glm::mat3(views[i]))));
                cmds.BindVertexBuffer(cubeVertexBuffer);
                cmds.Draw(36);
                cmds.EndRendering();
            }
            cmds.Barrier(
                {},
                {ImageMemoryBarrier(
                    *cubeMap, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                    VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                    VK_ACCESS_2_SHADER_READ_BIT, VK_IMAGE_ASPECT_COLOR_BIT, 0,
                    Image::CubemapFaces, 0, 1)},
                {});
        });

        auto cubeMapView = std::make_unique<ImageView>(
            renderer.GetDevice(), *cubeMap, VK_IMAGE_VIEW_TYPE_CUBE,
            VK_IMAGE_ASPECT_COLOR_BIT, 0, Image::CubemapFaces, 0, 1);
        return {std::move(cubeMap), std::move(cubeMapView)};
    }

    TextureCube VisibilityBufferBackend::CalculateDiffuseIrradiance(
        Renderer &renderer, Image &depthImage, ImageView &cubeMap)
    {
        auto cubeVertexBuffer = CreateCubeVertexBuffer(renderer);

        DescriptorSetLayout cubeSetLayout(
            renderer.GetDevice(),
            {DescriptorSetLayout::Binding(
                0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                VK_SHADER_STAGE_FRAGMENT_BIT, 1)});
        PipelineLayout cubePipeLayout(
            renderer.GetDevice(), {std::ref(cubeSetLayout)},
            {PushConstantRange(VK_SHADER_STAGE_VERTEX_BIT, sizeof(EqMapData))});

        GraphicsPipeline cubePipe(
            renderer.GetDevice(),
            GraphicsPipelineDesc(
                cubePipeLayout,
                Shader(renderer.GetDevice(),
                       "./Assets/Shaders/Bin/DiffuseIrradiance.spv")
                    .AddStage(VK_SHADER_STAGE_VERTEX_BIT, "VSMain")
                    .AddStage(VK_SHADER_STAGE_FRAGMENT_BIT, "FSMain"),
                {InputBinding(
                    {InputAttribute(0, VK_FORMAT_R32G32B32_SFLOAT, 0)},
                    VK_VERTEX_INPUT_RATE_VERTEX, sizeof(float) * 3)},
                InputAssembly(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST),
                Rasterizer(VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE,
                           VK_POLYGON_MODE_FILL),
                Multisample(VK_SAMPLE_COUNT_1_BIT),
                {ColorAttachment(VK_FORMAT_R32G32B32A32_SFLOAT)},
                {DepthStencil(depthImage.GetFormat())}));

        auto cubeDescSet = mSetAllocator.Allocate(cubeSetLayout);
        cubeDescSet
            ->PushWrite(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, cubeMap,
                        renderer.GetDevice().GetSamplers().TrilinearColor())
            .Update();

        auto irradianceMap = std::make_unique<Image>(
            renderer.GetDevice(), VK_FORMAT_R32G32B32A32_SFLOAT,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            32, 32, 1, Image::CubemapFaces, VK_IMAGE_TYPE_2D, 1,
            VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT);
        auto irradianceMapFaces = ImageView::CreateFacesForCubemap(
            renderer.GetDevice(), *irradianceMap, VK_IMAGE_ASPECT_COLOR_BIT);
        std::array views{
            glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f),
                        glm::vec3(1.0f, 0.0f, 0.0f),
                        glm::vec3(0.0f, 1.0f, 0.0f)),
            glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f),
                        glm::vec3(-1.0f, 0.0f, 0.0f),
                        glm::vec3(0.0f, 1.0f, 0.0f)),
            glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f),
                        glm::vec3(0.0f, 1.0f, 0.0f),
                        glm::vec3(0.0f, 0.0f, 1.0f)),
            glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f),
                        glm::vec3(0.0f, -1.0f, 0.0f),
                        glm::vec3(0.0f, 0.0f, -1.0f)),
            glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f),
                        glm::vec3(0.0f, 0.0f, -1.0f),
                        glm::vec3(0.0f, 1.0f, 0.0f)),
            glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f),
                        glm::vec3(0.0f, 0.0f, 1.0f),
                        glm::vec3(0.0f, 1.0f, 0.0f)),
        };

        renderer.GetDevice().RunImmediateCommands([&](CommandBuffer &cmds) {
            glm::mat4 proj =
                glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 100.0f);
            cmds.Barrier(
                {},
                {ImageMemoryBarrier(
                    *irradianceMap, VK_IMAGE_LAYOUT_UNDEFINED,
                    VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_ACCESS_2_NONE,
                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                    VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                    VK_IMAGE_ASPECT_COLOR_BIT, 0, Image::CubemapFaces, 0, 1)},
                {});
            cmds.SetViewportAndScissor(
                {irradianceMap->GetWidth(), irradianceMap->GetHeight()});
            for (uint32_t i = 0; i < 6; ++i)
            {
                cmds.BeginRendering(
                    {ColorAttachment(irradianceMapFaces[i]->Get(),
                                     VK_ATTACHMENT_LOAD_OP_CLEAR,
                                     VK_ATTACHMENT_STORE_OP_STORE)},
                    Scissor({irradianceMap->GetWidth(),
                             irradianceMap->GetHeight()}));
                cmds.BindGraphicsPipeline(cubePipe);
                cmds.BindGraphicsDescriptorSets(cubePipeLayout, 0,
                                                {std::ref(*cubeDescSet)});
                cmds.PushConstants(
                    cubePipeLayout, VK_SHADER_STAGE_VERTEX_BIT,
                    EqMapData(proj * glm::mat4(glm::mat3(views[i]))));
                cmds.BindVertexBuffer(cubeVertexBuffer);
                cmds.Draw(36);
                cmds.EndRendering();
            }
            cmds.Barrier(
                {},
                {ImageMemoryBarrier(
                    *irradianceMap, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                    VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                    VK_ACCESS_2_SHADER_READ_BIT, VK_IMAGE_ASPECT_COLOR_BIT, 0,
                    Image::CubemapFaces, 0, 1)},
                {});
        });
        auto irradianceMapView = std::make_unique<ImageView>(
            renderer.GetDevice(), *irradianceMap, VK_IMAGE_VIEW_TYPE_CUBE,
            VK_IMAGE_ASPECT_COLOR_BIT, 0, Image::CubemapFaces, 0, 1);
        return {std::move(irradianceMap), std::move(irradianceMapView)};
    }

    TextureCube VisibilityBufferBackend::PrefilterEnvMap(Renderer &renderer,
                                                         Image &depthImage,
                                                         ImageView &cubeMap)
    {
        auto cubeVertexBuffer = CreateCubeVertexBuffer(renderer);

        DescriptorSetLayout cubeSetLayout(
            renderer.GetDevice(),
            {DescriptorSetLayout::Binding(
                0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                VK_SHADER_STAGE_FRAGMENT_BIT, 1)});
        PipelineLayout cubePipeLayout(
            renderer.GetDevice(), {std::ref(cubeSetLayout)},
            {PushConstantRange(VK_SHADER_STAGE_VERTEX_BIT |
                                   VK_SHADER_STAGE_FRAGMENT_BIT,
                               sizeof(PrefilterData))});

        GraphicsPipeline cubePipe(
            renderer.GetDevice(),
            GraphicsPipelineDesc(
                cubePipeLayout,
                Shader(renderer.GetDevice(),
                       "./Assets/Shaders/Bin/Prefilter.spv")
                    .AddStage(VK_SHADER_STAGE_VERTEX_BIT, "VSMain")
                    .AddStage(VK_SHADER_STAGE_FRAGMENT_BIT, "FSMain"),
                {InputBinding(
                    {InputAttribute(0, VK_FORMAT_R32G32B32_SFLOAT, 0)},
                    VK_VERTEX_INPUT_RATE_VERTEX, sizeof(float) * 3)},
                InputAssembly(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST),
                Rasterizer(VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE,
                           VK_POLYGON_MODE_FILL),
                Multisample(VK_SAMPLE_COUNT_1_BIT),
                {ColorAttachment(VK_FORMAT_R32G32B32A32_SFLOAT)},
                {DepthStencil(depthImage.GetFormat())}));

        auto cubeDescSet = mSetAllocator.Allocate(cubeSetLayout);
        cubeDescSet
            ->PushWrite(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, cubeMap,
                        renderer.GetDevice().GetSamplers().TrilinearColor())
            .Update();

        auto res = std::make_unique<Image>(
            renderer.GetDevice(), VK_FORMAT_R32G32B32A32_SFLOAT,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            128, 128, 1, Image::CubemapFaces, VK_IMAGE_TYPE_2D,
            std::min(Image::GetMaxMipLevels(128, 128), 5u),
            VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT);
        auto resViews = ImageView::CreateFacesForCubemap(
            renderer.GetDevice(), *res, VK_IMAGE_ASPECT_COLOR_BIT, 0,
            res->GetMipLevels());

        std::array views{
            glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f),
                        glm::vec3(1.0f, 0.0f, 0.0f),
                        glm::vec3(0.0f, 1.0f, 0.0f)),
            glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f),
                        glm::vec3(-1.0f, 0.0f, 0.0f),
                        glm::vec3(0.0f, 1.0f, 0.0f)),
            glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f),
                        glm::vec3(0.0f, 1.0f, 0.0f),
                        glm::vec3(0.0f, 0.0f, 1.0f)),
            glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f),
                        glm::vec3(0.0f, -1.0f, 0.0f),
                        glm::vec3(0.0f, 0.0f, -1.0f)),
            glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f),
                        glm::vec3(0.0f, 0.0f, -1.0f),
                        glm::vec3(0.0f, 1.0f, 0.0f)),
            glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f),
                        glm::vec3(0.0f, 0.0f, 1.0f),
                        glm::vec3(0.0f, 1.0f, 0.0f)),
        };

        renderer.GetDevice().RunImmediateCommands([&](CommandBuffer &cmds) {
            glm::mat4 proj =
                glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 100.0f);
            cmds.Barrier(
                {},
                {ImageMemoryBarrier(
                    *res, VK_IMAGE_LAYOUT_UNDEFINED,
                    VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_ACCESS_2_NONE,
                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                    VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                    VK_IMAGE_ASPECT_COLOR_BIT, 0, Image::CubemapFaces, 0,
                    res->GetMipLevels())},
                {});

            for (uint32_t level = 0; level < res->GetMipLevels(); ++level)
            {
                uint32_t levelWidth = res->GetWidth() * std::pow(0.5, level);
                uint32_t levelHeight = res->GetHeight() * std::pow(0.5, level);
                cmds.SetViewportAndScissor({levelWidth, levelHeight});
                const float roughness =
                    level / (float)(res->GetMipLevels() - 1);

                for (uint32_t i = 0; i < 6; ++i)
                {
                    cmds.BeginRendering(
                        {ColorAttachment(resViews[level * 6 + i]->Get(),
                                         VK_ATTACHMENT_LOAD_OP_CLEAR,
                                         VK_ATTACHMENT_STORE_OP_STORE)},
                        Scissor({levelWidth, levelHeight}));
                    cmds.BindGraphicsPipeline(cubePipe);
                    cmds.BindGraphicsDescriptorSets(cubePipeLayout, 0,
                                                    {std::ref(*cubeDescSet)});
                    cmds.PushConstants(
                        cubePipeLayout,
                        VK_SHADER_STAGE_VERTEX_BIT |
                            VK_SHADER_STAGE_FRAGMENT_BIT,
                        PrefilterData(proj * glm::mat4(glm::mat3(views[i])),
                                      roughness));
                    cmds.BindVertexBuffer(cubeVertexBuffer);
                    cmds.Draw(36);
                    cmds.EndRendering();
                }
            }

            cmds.Barrier(
                {},
                {ImageMemoryBarrier(
                    *res, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                    VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                    VK_ACCESS_2_SHADER_READ_BIT, VK_IMAGE_ASPECT_COLOR_BIT, 0,
                    Image::CubemapFaces, 0, res->GetMipLevels())},
                {});
        });

        auto resView = std::make_unique<ImageView>(
            renderer.GetDevice(), *res, VK_IMAGE_VIEW_TYPE_CUBE,
            VK_IMAGE_ASPECT_COLOR_BIT, 0, Image::CubemapFaces, 0,
            res->GetMipLevels());
        return {std::move(res), std::move(resView)};
    }

    Texture2D VisibilityBufferBackend::GenerateBrdfLut(Renderer &renderer,
                                                       Image &depthImage)
    {
        PipelineLayout cubePipeLayout(renderer.GetDevice(), {}, {});

        GraphicsPipeline cubePipe(
            renderer.GetDevice(),
            GraphicsPipelineDesc(
                cubePipeLayout,
                Shader(renderer.GetDevice(),
                       "./Assets/Shaders/Bin/IntegrateBRDF.spv")
                    .AddStage(VK_SHADER_STAGE_VERTEX_BIT, "VSMain")
                    .AddStage(VK_SHADER_STAGE_FRAGMENT_BIT, "FSMain"),
                {}, InputAssembly(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST),
                Rasterizer(VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE,
                           VK_POLYGON_MODE_FILL),
                Multisample(VK_SAMPLE_COUNT_1_BIT),
                {ColorAttachment(VK_FORMAT_R32G32B32A32_SFLOAT)},
                {DepthStencil(depthImage.GetFormat())}));

        auto res = std::make_unique<Image>(
            renderer.GetDevice(), VK_FORMAT_R32G32B32A32_SFLOAT,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            512, 512, 1, 1, VK_IMAGE_TYPE_2D, 1);
        auto resView = std::make_unique<ImageView>(
            renderer.GetDevice(), *res, VK_IMAGE_VIEW_TYPE_2D,
            VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1);

        renderer.GetDevice().RunImmediateCommands([&](CommandBuffer &cmds) {
            cmds.Barrier(
                {},
                {ImageMemoryBarrier(
                    *res, VK_IMAGE_LAYOUT_UNDEFINED,
                    VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_ACCESS_2_NONE,
                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                    VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                    resView->GetAspect(), 0, 1, 0, 1)},
                {});
            cmds.SetViewportAndScissor({res->GetWidth(), res->GetHeight()});
            cmds.BeginRendering(
                {ColorAttachment(resView->Get(), VK_ATTACHMENT_LOAD_OP_CLEAR,
                                 VK_ATTACHMENT_STORE_OP_STORE)},
                Scissor({res->GetWidth(), res->GetHeight()}));
            cmds.BindGraphicsPipeline(cubePipe);
            cmds.Draw(6);
            cmds.EndRendering();
            cmds.Barrier({},
                         {ImageMemoryBarrier(
                             *res, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                             VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                             VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                             VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                             VK_ACCESS_2_SHADER_READ_BIT, resView->GetAspect(),
                             0, 1, 0, 1)},
                         {});
        });
        return {std::move(res), std::move(resView)};
    }

    Buffer VisibilityBufferBackend::CreateCubeVertexBuffer(Renderer &renderer)
    {
        constexpr std::array cubeVertices{
            // back face
            -1.0f,
            -1.0f,
            -1.0f,
            1.0f,
            1.0f,
            -1.0f,
            1.0f,
            -1.0f,
            -1.0f,
            1.0f,
            1.0f,
            -1.0f,
            -1.0f,
            -1.0f,
            -1.0f,
            -1.0f,
            1.0f,
            -1.0f,
            // front face
            -1.0f,
            -1.0f,
            1.0f,
            1.0f,
            -1.0f,
            1.0f,
            1.0f,
            1.0f,
            1.0f,
            1.0f,
            1.0f,
            1.0f,
            -1.0f,
            1.0f,
            1.0f,
            -1.0f,
            -1.0f,
            1.0f,
            // left face
            -1.0f,
            1.0f,
            1.0f,
            -1.0f,
            1.0f,
            -1.0f,
            -1.0f,
            -1.0f,
            -1.0f,
            -1.0f,
            -1.0f,
            -1.0f,
            -1.0f,
            -1.0f,
            1.0f,
            -1.0f,
            1.0f,
            1.0f,
            // right face
            1.0f,
            1.0f,
            1.0f,
            1.0f,
            -1.0f,
            -1.0f,
            1.0f,
            1.0f,
            -1.0f,
            1.0f,
            -1.0f,
            -1.0f,
            1.0f,
            1.0f,
            1.0f,
            1.0f,
            -1.0f,
            1.0f,
            // bottom face
            -1.0f,
            -1.0f,
            -1.0f,
            1.0f,
            -1.0f,
            -1.0f,
            1.0f,
            -1.0f,
            1.0f,
            1.0f,
            -1.0f,
            1.0f,
            -1.0f,
            -1.0f,
            1.0f,
            -1.0f,
            -1.0f,
            -1.0f,
            // top face
            -1.0f,
            1.0f,
            -1.0f,
            1.0f,
            1.0f,
            1.0f,
            1.0f,
            1.0f,
            -1.0f,
            1.0f,
            1.0f,
            1.0f,
            -1.0f,
            1.0f,
            -1.0f,
            -1.0f,
            1.0f,
            1.0f,
        };

        return Buffer(
            renderer.GetDevice(),
            BufferDesc::Upload(cubeVertices.size() * sizeof(cubeVertices[0]),
                               VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                                   VK_BUFFER_USAGE_TRANSFER_SRC_BIT),
            cubeVertices.data());
    }
} // namespace im