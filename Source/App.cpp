#include "App.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <numeric>
#include <optional>
#include <string_view>

#include <imgui.h>
#include <stb_image.h>

#include "Renderer.h"
#include "Utils.h"

namespace im
{
    App::App()
        : mWindow("Vulkan App"), mRenderer(mWindow),
          mAssetManager(mRenderer.GetDevice()), mScene(mAssetManager, mRenderer)
    {
        InitWindow();
        mRenderer.InitImGui();
    }

    void App::Run()
    {
        double lastTime = glfwGetTime();
        std::array<double, 40> totalMspf;
        totalMspf.fill(0.0);
        while (!mWindow.ShouldClose())
        {
            glfwPollEvents();

            const auto currentTime = glfwGetTime();
            const auto deltaTimeSecs = currentTime - lastTime;
            lastTime = currentTime;

            Update(deltaTimeSecs);
            Render();

            std::shift_left(totalMspf.begin(), totalMspf.end(), 1);
            totalMspf.back() = deltaTimeSecs * 1000.0;
            mWindow.SetTitle(fmt::format(
                "Vulkan App - {:.1f} ms",
                std::accumulate(totalMspf.begin(), totalMspf.end(), 0.0) /
                    (double)totalMspf.size()));
        }
    }

    void App::Update(float deltaTime)
    {
        if (glfwGetKey(mWindow.Get(), GLFW_KEY_ESCAPE) == GLFW_PRESS)
            glfwSetWindowShouldClose(mWindow.Get(), GLFW_TRUE);

        mScene.Update(deltaTime);
    }

    void App::Render()
    {
        mRenderer.Render(mScene);
    }

    void App::InitWindow()
    {
        glfwSetWindowUserPointer(mWindow.Get(), this);
        glfwSetFramebufferSizeCallback(mWindow.Get(), FramebufferSizeCallback);
        glfwSetCursorPosCallback(mWindow.Get(), MousePositionCallback);
        glfwSetKeyCallback(mWindow.Get(), KeyCallback);
        mWindow.SetCursorLocked(true);
    }

    void App::FramebufferSizeCallback(GLFWwindow *window, int width, int height)
    {
        App *app = reinterpret_cast<App *>(glfwGetWindowUserPointer(window));
        app->mRenderer.mFramebufferResized = true;
    }

    void App::MousePositionCallback(GLFWwindow *window, double xpos,
                                    double ypos)
    {
        static bool firstTouch = true;
        static double lastX;
        static double lastY;
        App *app = reinterpret_cast<App *>(glfwGetWindowUserPointer(window));
        if (!app->mWindow.IsCursorLocked())
            return;

        if (firstTouch)
        {
            firstTouch = false;
            lastX = xpos;
            lastY = ypos;
        }

        float deltaX = lastX - xpos;
        float deltaY = lastY - ypos;

        constexpr float sensitivity = 0.2f;
        auto &rotation = app->mScene.mCamera.rotation;
        rotation = glm::angleAxis(glm::radians(sensitivity * deltaY),
                                  rotation * glm::vec3(1.0f, 0.0f, 0.0f)) *
                   rotation;
        rotation = glm::angleAxis(glm::radians(sensitivity * deltaX),
                                  glm::vec3(0.0f, 1.0f, 0.0f)) *
                   rotation;
        rotation = glm::normalize(rotation);
        glfwSetCursorPos(window, lastX, lastY);
    }

    void App::KeyCallback(GLFWwindow *window, int key, int scanCode, int action,
                          int mods)
    {
        if (key == GLFW_KEY_K && action == GLFW_PRESS)
        {
            auto *app =
                reinterpret_cast<App *>(glfwGetWindowUserPointer(window));
            app->mWindow.SetCursorLocked(!app->mWindow.IsCursorLocked());
        }
    }
} // namespace im