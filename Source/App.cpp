#include "App.h"

#include <array>
#include <optional>
#include <algorithm>
#include <fstream>
#include <unordered_map>
#include <string_view>
#include <numeric>

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
		double lastTime = glfwGetTime();
		std::array<double, 40> totalMspf;
		totalMspf.fill(0.0);
		while (!mWindow.ShouldClose())
		{
			glfwPollEvents();
			const double currentTime = glfwGetTime();
			const double deltaTime = currentTime - lastTime;
			const double deltaTimeMs = deltaTime * 1000.0;
			
			Update(deltaTime);
			Render();

			std::shift_left(totalMspf.begin(), totalMspf.end(), 1);
			totalMspf.back() = deltaTimeMs;
			mWindow.SetTitle(std::format("Vulkan App - {:.1f} ms", std::accumulate(totalMspf.begin(), totalMspf.end(), 0.0) / (double)totalMspf.size()));
			
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