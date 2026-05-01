#pragma once

#include "input/input.h"

#include <string>

namespace GameWIP::Platform::Win32
{
    struct WindowDescription // Configuration used when creating a native window.
    {
        std::string title;
        int width;
        int height;
    };

    class Window
    {
    private:
        struct NativeWindow;
        NativeWindow *nativeWindow = nullptr;

    public:
        // Native window handles should not be copied.
        Window() = default;
        Window(const Window &) = delete;
        Window &operator=(const Window &) = delete;
        ~Window();

        /// @brief Creates the native Win32 window.
        /// @param description Window title and requested size.
        /// @return Return true on success.
        bool create(const WindowDescription &description);

        /// @brief Destroys the native window and clears owned native state.
        void destroy();

        /// @brief Processes pending Win32 messages.
        /// @param inputState Input state updated from key messages.
        void pollEvents(Input::InputState &inputState);

        /// @brief Returns whether the program has requested shutdown.
        /// @return True if the game loop should exit.
        bool shouldClose() const;

        /// @brief Marks the window as wanting to close.
        void requestClose();

        /// @brief Returns current window width.
        /// @return The width.
        int getClientWidth() const;

        /// @brief Returns current window height.
        /// @return The height.
        int getClientHeight() const;

        /// @brief Updates stored client area size from Win32 resize messages.
        /// @param width Width.
        /// @param height Height.
        void handleResize(int width, int height);
    };
}