#pragma once

#include "Common.h"

namespace im
{
    class Window
    {
    public:
        static constexpr uint32_t DefaultWidth{1280};
        static constexpr uint32_t DefaultHeight{720};

    public:
        Window(std::string_view title, uint32_t width = DefaultWidth,
               uint32_t height = DefaultHeight);
        ~Window();

        Window(Window &&other) noexcept = delete;
        Window &operator=(Window &&other) noexcept = delete;

        Window(const Window &other) = delete;
        Window &operator=(const Window &other) = delete;

        GLFWwindow *Get()
        {
            return mWindow;
        }

        bool ShouldClose() const;
        bool IsCursorLocked() const
        {
            return mCursorLocked;
        }

        bool IsKeyPressed(int keyCode) const;

        void WaitForNonMinimized();

        void SetCursorLocked(bool locked);
        void SetTitle(std::string_view title);

    private:
        GLFWwindow *mWindow;

        bool mCursorLocked{false};
    };
} // namespace im