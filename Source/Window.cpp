#include "Window.h"

namespace im
{
	Window::Window(const char* title, uint32_t width, uint32_t height)
	{
		glfwInit();
		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
		mWindow = glfwCreateWindow(width, height, title, nullptr, nullptr);
		if (!mWindow)
		{
			fmt::println(stderr, "Failed to create window!");
			return;
		}
	}

	Window::~Window()
	{
		glfwTerminate();
	}

	bool Window::ShouldClose() const
	{
		return glfwWindowShouldClose(mWindow) == GLFW_TRUE;
	}

	void Window::WaitForNonMinimized()
	{
		int width = 0;
		int height = 0;
		glfwGetFramebufferSize(mWindow, &width, &height);
		while (width == 0 || height == 0)
		{
			glfwGetFramebufferSize(mWindow, &width, &height);
			glfwWaitEvents();
		}
	}

	void Window::SetCursorLocked(bool locked)
	{
		mCursorLocked = locked;
		glfwSetInputMode(mWindow, GLFW_CURSOR, locked ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
	}
}