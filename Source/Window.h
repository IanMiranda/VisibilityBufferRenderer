#pragma once

#include "Common.h"

namespace im
{
	class Window
	{
	public:
		Window(uint32_t width, uint32_t height, const char* title);
		~Window();

		Window(Window&& other) noexcept = delete;
		Window& operator=(Window&& other) noexcept = delete;

		Window(const Window& other) = delete;
		Window& operator=(const Window& other) = delete;

		GLFWwindow* Get() { return mWindow; }

		bool ShouldClose() const;

		void WaitForNonMinimized();

	private:
		GLFWwindow* mWindow;
	};
}