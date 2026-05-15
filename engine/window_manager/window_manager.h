#pragma once

#include "window/window.h"

#include <vector>

namespace GameWIP::Input
{
    class InputDeviceRegistry;
    class InputState;
}

namespace GameWIP
{
    /// @brief Owns and routes events for platform windows.
    class WindowManager
    {
    public:
        WindowManager();
        ~WindowManager();

        WindowManager(const WindowManager &) = delete;
        WindowManager &operator=(const WindowManager &) = delete;
        WindowManager(WindowManager &&) = delete;
        WindowManager &operator=(WindowManager &&) = delete;

        Window::Result createWindow(const Window::Description &description, Window *&outWindow);
        Window::Result destroyWindow(Window &window);

        void pollEvents(Input::InputState &gameInput, Input::InputDeviceRegistry &inputDevices, Input::InputState *toolInput);
        bool shouldQuit() const;

        Window *getMainWindow() const;
        Window *getFocusedWindow() const;
        Window *getToolInputWindow() const;
        void getWindows(std::vector<Window *> &outWindows) const;

    private:
        struct NativeWindowManager;

        NativeWindowManager *nativeManager = nullptr;
    };
}
