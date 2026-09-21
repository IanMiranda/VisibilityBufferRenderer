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
          mCamera(70,
                  static_cast<float>(
                      renderer.GetDevice().GetSwapchain().GetExtent().width) /
                      renderer.GetDevice().GetSwapchain().GetExtent().height,
                  0.1f, 100.0f)
    {
        mNodes = assets.LoadGltfScene("Assets/Models/Sponza/glTF/Sponza.gltf");

        mPointLights.resize(500);

        std::random_device randDevice;
        std::mt19937 generator(randDevice());
        std::uniform_real_distribution<float> distrib(-15.0f, 15.0f);

        for (auto &light : mPointLights)
        {
            light.position = glm::vec3(distrib(generator), distrib(generator),
                                       distrib(generator));
            light.i =
                glm::vec4(glm::vec3(std::fabs(distrib(generator) / 10.0f),
                                    std::fabs(distrib(generator) / 10.0f),
                                    std::fabs(distrib(generator) / 10.0f)),
                          1.0f);
        }
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