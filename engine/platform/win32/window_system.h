#pragma once

#include "window.h"

#include <vector>

namespace GameWIP::Platform::Win32
{
    /// @brief Owns and routes events for Win32 windows created by the platform layer.
    class WindowSystem
    {
    public:
        WindowSystem();
        ~WindowSystem();

        WindowSystem(const WindowSystem &) = delete;
        WindowSystem &operator=(const WindowSystem &) = delete;
        WindowSystem(WindowSystem &&) = delete;
        WindowSystem &operator=(WindowSystem &&) = delete;

        WindowResult createWindow(const WindowDescription &description, Window *&outWindow);
        WindowResult destroyWindow(Window &window);

        void pollEvents(Input::InputState &gameInput, Input::InputState *toolInput);
        bool shouldQuit() const;

        Window *getMainWindow() const;
        Window *getFocusedWindow() const;
        Window *getToolInputWindow() const;
        void getWindows(std::vector<Window *> &outWindows) const;

    private:
        struct NativeWindowSystem;

        NativeWindowSystem *nativeSystem = nullptr;
    };
}
