#pragma once

#include <filesystem>

#include "AssetManager.h"
#include "Camera.h"
#include "Common.h"
#include "Renderer.h"
#include "Scene.h"
#include "Window.h"

namespace im
{
    class Renderer;

    class App
    {
    public:
        App();

        App(App &&other) noexcept = delete;
        App &operator=(App &&other) noexcept = delete;

        App(const App &other) = delete;
        App &operator=(const App &other) = delete;

        void Run();

        Window &GetWindow()
        {
            return mWindow;
        }
        Renderer &GetRenderer()
        {
            return mRenderer;
        }

    private:
        void Update(float deltaTime);
        void Render();

    private:
        void InitWindow();

    private:
        Window mWindow;
        Renderer mRenderer;
        AssetManager mAssetManager;
        Scene mScene;

    private:
        static void FramebufferSizeCallback(GLFWwindow *window, int width,
                                            int height);
        static void MousePositionCallback(GLFWwindow *window, double xpos,
                                          double ypos);
        static void KeyCallback(GLFWwindow *window, int key, int scanCode,
                                int action, int mods);
    };
} // namespace im
