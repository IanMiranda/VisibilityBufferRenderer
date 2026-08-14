#include "Scene.h"

#include <imgui.h>
#include <numbers>
#include <numeric>
#include <stb_image.h>
#include <unordered_set>

#include "API/CommandBuffer.h"
#include "Common.h"
#include "Renderer.h"
#include "Utils.h"
#include "vulkan/vulkan_core.h"

namespace im
{
    Scene::Scene(Renderer &renderer)
        : mRenderer(renderer),
          mCamera(75,
                  static_cast<float>(
                      renderer.GetDevice().GetSwapchain().GetExtent().width) /
                      renderer.GetDevice().GetSwapchain().GetExtent().height,
                  0.1f, 100.0f)
    {
        mMaterial = Material{
            CreateAndStageTexture("./Assets/Models/Helmet/Default_albedo.jpg",
                                  VK_FORMAT_R8G8B8A8_SRGB, false),
            CreateAndStageTexture(
                "./Assets/Models/Helmet/Default_metalRoughness.jpg",
                VK_FORMAT_R8G8B8A8_UNORM, false),
            CreateAndStageTexture(
                "./Assets/Models/Helmet/Default_metalRoughness.jpg",
                VK_FORMAT_R8G8B8A8_UNORM, false),
            CreateAndStageTexture("./Assets/Models/Helmet/Default_normal.jpg",
                                  VK_FORMAT_R8G8B8A8_UNORM, false),
            CreateAndStageTexture("./Assets/Models/Helmet/Default_AO.jpg",
                                  VK_FORMAT_R8G8B8A8_UNORM, false),
            CreateAndStageTexture("./Assets/Models/Helmet/Default_emissive.jpg",
                                  VK_FORMAT_R8G8B8A8_SRGB, false),
        };

        const auto [helmetVertices, helmetIndices] =
            utils::LoadGltfModel("./Assets/Models/Helmet/DamagedHelmet2.gltf");

        Buffer stagingVbo(
            renderer.GetDevice(),
            BufferDesc(helmetVertices.size() * sizeof(helmetVertices[0]),
                       VK_BUFFER_USAGE_2_TRANSFER_SRC_BIT,
                       VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT),
            helmetVertices.data());

        Buffer stagingIbo(
            renderer.GetDevice(),
            BufferDesc(helmetIndices.size() * sizeof(helmetIndices[0]),
                       VK_BUFFER_USAGE_2_TRANSFER_SRC_BIT,
                       VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT),
            helmetIndices.data());
        auto vertexBuffer = std::make_unique<Buffer>(
            renderer.GetDevice(),
            BufferDesc(helmetVertices.size() * sizeof(helmetVertices[0]),
                       VK_BUFFER_USAGE_2_TRANSFER_DST_BIT |
                           VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT |
                           VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT));
        auto indexBuffer = std::make_unique<Buffer>(
            renderer.GetDevice(),
            BufferDesc(helmetIndices.size() * sizeof(helmetIndices[0]),
                       VK_BUFFER_USAGE_2_TRANSFER_DST_BIT |
                           VK_BUFFER_USAGE_2_INDEX_BUFFER_BIT |
                           VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT |
                           VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT));

        mRenderer.GetDevice().RunImmediateCommands([&](CommandBuffer &cmd) {
            cmd.Copy(stagingVbo, *vertexBuffer);
            cmd.Copy(stagingIbo, *indexBuffer);
        });

        auto helmetMesh = std::make_shared<VbMesh>(
            std::move(vertexBuffer), std::move(indexBuffer),
            static_cast<uint32_t>(helmetIndices.size()));

        for (float z = -3.0f; z <= 3.0f; z += 1.0f)
        {
            for (float x = -3.0f; x <= 3.0f; x += 1.0f)
            {
                for (float y = -3.0f; y <= 3.0f; y += 1.0f)
                {
                    glm::mat4 model =
                        glm::translate(glm::mat4(1.0f),
                                       glm::vec3(x * 5.0f, y * 5.0f, z * 5.0f));
                    model = glm::rotate(model, glm::radians(90.0f),
                                        glm::vec3(1.0f, 0.0f, 0.0f));
                    model = glm::scale(model, glm::vec3(2.0f));
                    mObjects.emplace_back(helmetMesh, mMaterial, model);
                }
            }
        }

        mPointLights.resize(8);
        // CombineMeshBuffers();

        // Info to create the Blas
        /*VkAccelerationStructureGeometryTrianglesDataKHR
        triData{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR};
        triData.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
        triData.vertexData.deviceAddress = mVertexBuffer->GetAddress();
        triData.vertexStride = sizeof(Vertex); triData.maxVertex =
        helmetVertices.size() - 1; triData.indexType = VK_INDEX_TYPE_UINT32;
        triData.indexData.deviceAddress = mIndexBuffer->GetAddress();

        VkAccelerationStructureGeometryKHR blasGeom{
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR };
        blasGeom.geometry.triangles = triData;
        blasGeom.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
        blasGeom.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;

        VkAccelerationStructureBuildGeometryInfoKHR buildBlasInfo{
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR };
        buildBlasInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        buildBlasInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        buildBlasInfo.geometryCount = 1; buildBlasInfo.pGeometries = &blasGeom;

        VkAccelerationStructureBuildSizesInfoKHR blasSizeInfo{
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR }; const
        uint32_t primCount = helmetIndices.size() / 3;
        vkGetAccelerationStructureBuildSizesKHR(mRenderer.GetDevice().Get(),
            VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
            &buildBlasInfo, &primCount, &blasSizeInfo
        );

        Buffer blasBuffer(mRenderer.GetDevice(),
        blasSizeInfo.accelerationStructureSize,
        VK_BUFFER_USAGE_2_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR, 0); Buffer
        scratchBuffer(mRenderer.GetDevice(), blasSizeInfo.buildScratchSize,
        VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT |
        VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT, 0);
        buildBlasInfo.scratchData.deviceAddress = scratchBuffer.GetAddress();

        VkAccelerationStructureCreateInfoKHR blasInfo{
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR };
        blasInfo.buffer = blasBuffer.Get();
        blasInfo.size = blasSizeInfo.accelerationStructureSize;
        blasInfo.offset = 0;
        blasInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;

        VkAccelerationStructureKHR blas;
        VK_CHECK(vkCreateAccelerationStructureKHR(mRenderer.GetDevice().Get(),
        &blasInfo, nullptr, &blas)); buildBlasInfo.dstAccelerationStructure =
        blas;

        VkAccelerationStructureBuildRangeInfoKHR buildRange{};
        buildRange.firstVertex = 0;
        buildRange.primitiveOffset = 0;
        buildRange.primitiveCount = primCount;
        buildRange.transformOffset = 0;

        mRenderer.GetDevice().RunImmediateCommands([&](CommandBuffer& cmd)
            {
                VkAccelerationStructureBuildRangeInfoKHR* buildRanges[] = {
        &buildRange }; vkCmdBuildAccelerationStructuresKHR(cmd.Get(), 1,
        &buildBlasInfo, buildRanges);
            }
        );

        vkDestroyAccelerationStructureKHR(mRenderer.GetDevice().Get(), blas,
        nullptr);*/
    }

    void Scene::Update(float deltaTime)
    {
        const bool fast = glfwGetKey(mRenderer.GetWindow().Get(),
                                     GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS;
        const float moveFactor = fast ? 7.5f : 2.5f;

        if (mRenderer.GetWindow().IsKeyPressed(GLFW_KEY_W))
            mCamera.position +=
                mCamera.GetFrontVector() * deltaTime * moveFactor;
        else if (mRenderer.GetWindow().IsKeyPressed(GLFW_KEY_S))
            mCamera.position +=
                -mCamera.GetFrontVector() * deltaTime * moveFactor;

        if (mRenderer.GetWindow().IsKeyPressed(GLFW_KEY_A))
            mCamera.position +=
                -mCamera.GetRightVector() * deltaTime * moveFactor;
        else if (mRenderer.GetWindow().IsKeyPressed(GLFW_KEY_D))
            mCamera.position +=
                mCamera.GetRightVector() * deltaTime * moveFactor;

        UpdateLightPositions();
    }

    void Scene::Render()
    {
        mRenderer.DrawBatch(*this, mObjects);

        DrawUI();
    }

    void Scene::DrawUI()
    {
        // if (ImGui::Begin("Vulkan Renderer"))
        // {
        //     int lightNumber = 0;
        //     for (auto &light : mPointLights)
        //     {
        //         float color[4] = {light.i.r, light.i.g, light.i.b,
        //         light.i.a}; const std::string label = fmt::format("Light {}",
        //         lightNumber);
        //         ;
        //         ImGui::ColorPicker4(label.c_str(), color, 0, color);
        //         light.i = glm::vec4(color[0], color[1], color[2], color[3]);

        //         lightNumber++;
        //     }
        // }

        // ImGui::End();
    }

    void Scene::UpdateLightPositions()
    {
        float offset = 0.0f;
        float distance = 20.0f * sin(glfwGetTime()) + 21.0f;
        for (auto &light : mPointLights)
        {
            light.position =
                glm::vec3(distance * sin(glfwGetTime() + offset), 1.0f,
                          -distance * cos(glfwGetTime() + offset));
            offset += (2 * std::numbers::pi) / mPointLights.size();
        }
    }

    std::unique_ptr<Texture2D> Scene::CreateAndStageTexture(
        const std::filesystem::path &path, VkFormat format,
        bool generateMipmaps)
    {
        stbi_set_flip_vertically_on_load(true);

        int width, height, channels;
        stbi_uc *data = stbi_load(path.string().c_str(), &width, &height,
                                  &channels, STBI_rgb_alpha);
        if (!data)
        {
            fmt::println(stderr, "Failed to load image from {}!", path);
            return nullptr;
        }

        VkDeviceSize size = width * height * 4; // 4 bytes per pixel (RGBA8)
        Buffer stagingTex(
            mRenderer.GetDevice(),
            BufferDesc(size, VK_BUFFER_USAGE_2_TRANSFER_SRC_BIT,
                       VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT),
            data);

        auto resTex = std::make_unique<Image>(
            mRenderer.GetDevice(), format,
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                VK_IMAGE_USAGE_SAMPLED_BIT,
            width, height, 1, 1, VK_IMAGE_TYPE_2D,
            generateMipmaps ? Image::GetMaxMipLevels(width, height) : 1);
        auto resTexView = std::make_unique<ImageView>(
            mRenderer.GetDevice(), *resTex, VK_IMAGE_VIEW_TYPE_2D,
            VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, resTex->GetMipLevels());

        mRenderer.GetDevice().RunImmediateCommands([&resTex, &stagingTex,
                                                    generateMipmaps](
                                                       CommandBuffer &cmds) {
            cmds.Barrier(
                {},
                {ImageMemoryBarrier(*resTex, VK_IMAGE_LAYOUT_UNDEFINED,
                                    VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE,
                                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                    VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                                    VK_ACCESS_2_TRANSFER_WRITE_BIT,
                                    VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0,
                                    resTex->GetMipLevels())},
                {});

            cmds.Copy(stagingTex, *resTex, VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0);

            if (generateMipmaps)
            {
                cmds.GenerateMipmaps(
                    *resTex, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                    VK_ACCESS_2_SHADER_READ_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
            }
            else
            {
                cmds.Barrier(
                    {},
                    {ImageMemoryBarrier(
                        *resTex, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                        VK_ACCESS_2_TRANSFER_WRITE_BIT,
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                        VK_ACCESS_2_SHADER_READ_BIT, VK_IMAGE_ASPECT_COLOR_BIT,
                        0, 1, 0, resTex->GetMipLevels())},
                    {});
            }
        });

        return std::make_unique<Texture2D>(std::move(resTex),
                                           std::move(resTexView));
    }

    void Scene::CombineMeshBuffers()
    {
        /*std::unordered_set<std::shared_ptr<Mesh>> meshes;
        for (const auto& object : mObjects) {
            meshes.insert(object.mesh);
        }
        auto totalVertices = std::accumulate(meshes.begin(), meshes.end(), 0z,
        [](size_t res, const std::shared_ptr<Mesh>& mesh) { return res +
        mesh->vertices.size(); }); auto totalIndices =
        std::accumulate(meshes.begin(), meshes.end(), 0z, [](size_t res, const
        std::shared_ptr<Mesh>& mesh) { return res
        + mesh->indices.size(); });

        Buffer stagingVtx(
            mRenderer.GetDevice(),
            totalVertices * sizeof(Vertex),
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
        Buffer stagingIdx(
            mRenderer.GetDevice(),
            totalIndices * sizeof(uint32_t),
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);

        auto vertexData = reinterpret_cast<Vertex*>(stagingVtx.Map());
        auto indexData = reinterpret_cast<uint32_t*>(stagingIdx.Map());
        uint32_t sceneBufferIndex = 0;
        for (const auto& mesh : meshes)
        {
            std::memcpy(vertexData, mesh->vertices.data(), mesh->vertices.size()
        * sizeof(Vertex)); std::memcpy(indexData, mesh->indices.data(),
        mesh->indices.size() * sizeof(uint32_t)); vertexData +=
        mesh->vertices.size(); indexData += mesh->indices.size();
            mesh->sceneBufferIndex = sceneBufferIndex;
            sceneBufferIndex += mesh->vertices.size();
        }
        stagingVtx.Unmap();
        stagingIdx.Unmap();

        mRenderer.GetDevice().RunImmediateCommands([this, &stagingVtx,
        &stagingIdx](CommandBuffer& commandBuffer)
            {
                mVertexBuffer = std::make_unique<Buffer>(
                    mRenderer.GetDevice(),
                    stagingVtx.GetSize(),
                    VK_BUFFER_USAGE_TRANSFER_DST_BIT |
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
        VK_BUFFER_USAGE_2_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
        0); mIndexBuffer = std::make_unique<Buffer>( mRenderer.GetDevice(),
        stagingIdx.GetSize(), VK_BUFFER_USAGE_TRANSFER_DST_BIT |
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
        VK_BUFFER_USAGE_2_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
        0); commandBuffer.Copy(stagingVtx, *mVertexBuffer);
        commandBuffer.Copy(stagingIdx, *mIndexBuffer);
            });*/
    }
} // namespace im