#include "App.h"

#include <array>
#include <optional>
#include <algorithm>
#include <fstream>
#include <unordered_map>
#include <string_view>

#include <stb_image.h>
#include <imgui.h>

#include "Utils.h"
#include "Renderer.h"

namespace im
{
	App::App()
		: mWindow("Vulkan App")
		, mRenderer(mWindow)
		, mScene(*this)
	{
		InitWindow();
		mRenderer.InitImGui();
	}

	App::~App()
	{
	}

	void App::Run()
	{
		float lastTime = glfwGetTime();
		float fpsLast = glfwGetTime();
		int frames = 0;
		while (!mWindow.ShouldClose())
		{
			glfwPollEvents();
			const float currentTime = glfwGetTime();
			const float deltaTime = currentTime - lastTime;

			Update(deltaTime);
			Render();

			++frames;
			if (glfwGetTime() - fpsLast >= 1.0)
			{
				fmt::println("FPS: {}", frames);
				fpsLast = glfwGetTime();
				frames = 0;
			}

			lastTime = currentTime;
		}
	}

	void App::Update(float deltaTime)
	{
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

	void App::FramebufferSizeCallback(GLFWwindow* window, int width, int height)
	{
		App* app = reinterpret_cast<App*>(glfwGetWindowUserPointer(window));
		app->mRenderer.mFramebufferResized = true;
	}

	void App::MousePositionCallback(GLFWwindow* window, double xpos, double ypos)
	{
		// TODO: Move to scene
		static bool firstTouch = true;
		static double lastX;
		static double lastY;
		App* app = reinterpret_cast<App*>(glfwGetWindowUserPointer(window));
		if (!app->mWindow.IsCursorLocked()) return;

		if (firstTouch)
		{
			firstTouch = false;
			lastX = xpos;
			lastY = ypos;
		}

		float deltaX = xpos - lastX;
		float deltaY = lastY - ypos;

		constexpr float sensitivity = 0.2f;
		app->mScene.mCamera.yaw += sensitivity * deltaX;
		app->mScene.mCamera.pitch += sensitivity * deltaY;
		glfwSetCursorPos(window, lastX, lastY);
	}

    void App::KeyCallback(GLFWwindow *window, int key, int scanCode, int action, int mods)
    {
		if (key == GLFW_KEY_K && action == GLFW_PRESS)
		{
			auto* app = reinterpret_cast<App*>(glfwGetWindowUserPointer(window));
			app->mWindow.SetCursorLocked(!app->mWindow.IsCursorLocked());
		}
    }
}