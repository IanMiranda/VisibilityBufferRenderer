#include "Scene.h"

#include <imgui.h>
#include <numbers>
#include <numeric>
#include <random>
#include <stb_image.h>
#include <unordered_set>

#include "API/CommandBuffer.h"
#include "AssetManager.h"
#include "Common.h"
#include "Renderer.h"
#include "Utils.h"
#include "vulkan/vulkan_core.h"

namespace im
{
    Scene::Scene(AssetManager &assets, Renderer &renderer)
        : mRenderer(renderer),
          mCamera(75,
                  static_cast<float>(
                      renderer.GetDevice().GetSwapchain().GetExtent().width) /
                      renderer.GetDevice().GetSwapchain().GetExtent().height,
                  0.1f, 100.0f)
    {
        mNodes = assets.LoadGltfScene(
            "Assets/Models/bistro-master/Bistro_Godot.glb");
        fmt::println("{}", mNodes.size());

        mPointLights.resize(1024);

        std::random_device randDevice;
        std::mt19937 generator(randDevice());
        std::uniform_real_distribution<float> distrib(-20.0f, 20.0f);

        for (auto &light : mPointLights)
        {
            light.position = glm::vec3(distrib(generator), distrib(generator),
                                       distrib(generator));
            light.i = glm::vec4(glm::vec3(3.0f), 1.0f);
        }
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

    std::vector<RenderCommand> Scene::SerializeRenderCommands()
    {
        std::vector<RenderCommand> res;
        for (const auto &node : mNodes)
        {
            const auto &nodeCommands = GatherNodeRenderCommands(*node);
            res.insert(res.end(), nodeCommands.begin(), nodeCommands.end());
        }

        return res;
    }

    void Scene::UpdateLightPositions()
    {
        /*float offset = 0.0f;
        float distance = 20.0f * sin(glfwGetTime()) + 21.0f;
        for (auto &light : mPointLights)
        {
            light.position =
                glm::vec3(distance * sin(glfwGetTime() + offset), 1.0f,
                          -distance * cos(glfwGetTime() + offset));
            light.i = glm::vec4(10.0f, 10.0f, 10.0f, 1.0f);
            offset += (2 * std::numbers::pi) / mPointLights.size();
        }*/
    }

    std::vector<RenderCommand> Scene::GatherNodeRenderCommands(Node &node)
    {
        std::vector<RenderCommand> res;

        if (node.mesh)
        {
            RenderCommand current{};
            current.mesh = node.mesh;
            current.transform = node.transform;
            res.emplace_back(current);
        }

        for (const auto &child : node.children)
        {
            const auto &childCommands = GatherNodeRenderCommands(*child);
            res.insert(res.end(), childCommands.begin(), childCommands.end());
        }

        return res;
    }
} // namespace im