#include "window_system.h"
#include "input/platform/win32/win32_input.h"

#include <windows.h>

#include <algorithm>
#include <memory>
#include <unordered_map>

namespace
{
    namespace Win32Input = GameWIP::Input::Platform::Win32;

    using GameWIP::Platform::Win32::Window;
    using GameWIP::Platform::Win32::WindowDescription;
    using GameWIP::Platform::Win32::WindowInfo;
    using GameWIP::Platform::Win32::WindowResult;
    using GameWIP::Platform::Win32::WindowRole;
    using GameWIP::Platform::Win32::WindowSystem;

    HWND toNativeHandle(const Window &window)
    {
        WindowInfo info = window.getInfo();
        return static_cast<HWND>(info.handle);
    }

    bool containsWindow(const std::vector<std::unique_ptr<Window>> &windows, const Window *window)
    {
        return std::any_of(windows.begin(), windows.end(), [window](const std::unique_ptr<Window> &candidate) {
            return candidate.get() == window;
        });
    }
}

namespace GameWIP::Platform::Win32
{
    struct WindowSystem::NativeWindowSystem
    {
        std::vector<std::unique_ptr<Window>> windows;
        std::unordered_map<HWND, Window *> windowsByHandle;
        std::vector<Window *> pendingDestroy;
        Window *mainWindow = nullptr;
        Window *focusedWindow = nullptr;
        Window *toolInputWindow = nullptr;
        Window *rawInputWindow = nullptr;
        bool quitRequested = false;
    };

    WindowSystem::WindowSystem()
        : nativeSystem(new NativeWindowSystem)
    {
    }

    WindowSystem::~WindowSystem()
    {
        delete nativeSystem;
        nativeSystem = nullptr;
    }

    WindowResult WindowSystem::createWindow(const WindowDescription &description, Window *&outWindow)
    {
        outWindow = nullptr;

        if (nativeSystem == nullptr)
        {
            return WindowResult::NotCreated;
        }

        if (description.role == WindowRole::MainGame && nativeSystem->mainWindow != nullptr)
        {
            return WindowResult::OperationNotAllowed;
        }

        auto window = std::make_unique<Window>();
        WindowResult createResult = window->create(description);
        if (createResult != WindowResult::Success)
        {
            return createResult;
        }

        Window *createdWindow = window.get();
        HWND handle = toNativeHandle(*createdWindow);
        if (handle == nullptr)
        {
            createdWindow->destroy();
            return WindowResult::Win32CallFailed;
        }

        if (description.role == WindowRole::MainGame)
        {
            unsigned long rawInputError = 0;
            if (!Win32Input::registerInputDevices(handle, rawInputError))
            {
                createdWindow->destroy();
                return WindowResult::Win32CallFailed;
            }

            nativeSystem->mainWindow = createdWindow;
            nativeSystem->rawInputWindow = createdWindow;
        }

        nativeSystem->windowsByHandle.emplace(handle, createdWindow);
        nativeSystem->windows.push_back(std::move(window));

        if (createdWindow->isFocused())
        {
            nativeSystem->focusedWindow = createdWindow;
            nativeSystem->toolInputWindow = createdWindow->getRole() == WindowRole::Tool ? createdWindow : nullptr;
        }

        outWindow = createdWindow;
        return WindowResult::Success;
    }

    WindowResult WindowSystem::destroyWindow(Window &window)
    {
        if (nativeSystem == nullptr)
        {
            return WindowResult::NotCreated;
        }

        auto windowIt = std::find_if(nativeSystem->windows.begin(), nativeSystem->windows.end(), [&window](const std::unique_ptr<Window> &candidate) {
            return candidate.get() == &window;
        });

        if (windowIt == nativeSystem->windows.end())
        {
            return WindowResult::NotCreated;
        }

        Window *windowPtr = windowIt->get();
        HWND handle = toNativeHandle(*windowPtr);
        if (handle != nullptr)
        {
            nativeSystem->windowsByHandle.erase(handle);
        }

        if (nativeSystem->focusedWindow == windowPtr)
        {
            nativeSystem->focusedWindow = nullptr;
        }

        if (nativeSystem->toolInputWindow == windowPtr)
        {
            nativeSystem->toolInputWindow = nullptr;
        }

        if (nativeSystem->rawInputWindow == windowPtr)
        {
            nativeSystem->rawInputWindow = nullptr;
        }

        bool destroyingMainWindow = nativeSystem->mainWindow == windowPtr;
        if (destroyingMainWindow)
        {
            nativeSystem->mainWindow = nullptr;
            nativeSystem->quitRequested = true;
        }

        nativeSystem->pendingDestroy.erase(
            std::remove(nativeSystem->pendingDestroy.begin(), nativeSystem->pendingDestroy.end(), windowPtr),
            nativeSystem->pendingDestroy.end());

        WindowResult destroyResult = windowPtr->destroy();
        nativeSystem->windows.erase(windowIt);
        return destroyResult;
    }

    void WindowSystem::pollEvents(Input::InputState &gameInput, Input::InputState *toolInput)
    {
        if (nativeSystem == nullptr)
        {
            return;
        }

        std::vector<Window *> pendingDestroy = std::move(nativeSystem->pendingDestroy);
        nativeSystem->pendingDestroy.clear();
        for (Window *window : pendingDestroy)
        {
            if (window != nullptr && window != nativeSystem->mainWindow && containsWindow(nativeSystem->windows, window))
            {
                destroyWindow(*window);
            }
        }

        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
        {
            if (message.message == WM_QUIT)
            {
                nativeSystem->quitRequested = true;
                continue;
            }

            Window *targetWindow = nullptr;
            auto targetIt = nativeSystem->windowsByHandle.find(message.hwnd);
            if (targetIt != nativeSystem->windowsByHandle.end())
            {
                targetWindow = targetIt->second;
            }

            const bool targetIsMain = targetWindow != nullptr && targetWindow == nativeSystem->mainWindow;
            const bool targetWasFocusedTool = targetWindow != nullptr && targetWindow == nativeSystem->toolInputWindow;
            const bool mainFocusLost = targetIsMain && message.message == WM_KILLFOCUS;
            const bool toolFocusLost = targetWasFocusedTool && message.message == WM_KILLFOCUS;

            if (message.message == WM_SETFOCUS && targetWindow != nullptr)
            {
                if (nativeSystem->toolInputWindow != nullptr && nativeSystem->toolInputWindow != targetWindow && toolInput != nullptr)
                {
                    toolInput->clear();
                }

                nativeSystem->focusedWindow = targetWindow;
                nativeSystem->toolInputWindow = targetWindow->getRole() == WindowRole::Tool ? targetWindow : nullptr;
            }

            if (message.message == WM_KILLFOCUS && targetWindow == nativeSystem->focusedWindow)
            {
                nativeSystem->focusedWindow = nullptr;
            }

            if (targetIsMain && targetWindow->isFocused())
            {
                Win32Input::handleMessage(
                    static_cast<unsigned int>(message.message),
                    static_cast<unsigned long long>(message.wParam),
                    static_cast<long long>(message.lParam),
                    gameInput);
            }

            if (toolInput != nullptr && targetWindow != nullptr && targetWindow->getRole() == WindowRole::Tool && targetWindow->isFocused())
            {
                nativeSystem->toolInputWindow = targetWindow;
                Win32Input::handleUiMessage(
                    static_cast<unsigned int>(message.message),
                    static_cast<unsigned long long>(message.wParam),
                    static_cast<long long>(message.lParam),
                    *toolInput);
            }

            TranslateMessage(&message);
            DispatchMessageW(&message);

            if (mainFocusLost)
            {
                gameInput.clear();
            }

            if (toolFocusLost)
            {
                if (toolInput != nullptr)
                {
                    toolInput->clear();
                }

                if (nativeSystem->toolInputWindow == targetWindow)
                {
                    nativeSystem->toolInputWindow = nullptr;
                }
            }
        }

        if (nativeSystem->mainWindow != nullptr && nativeSystem->mainWindow->isFocused())
        {
            Win32Input::updateGamepads(gameInput);
        }

        if (nativeSystem->mainWindow != nullptr && nativeSystem->mainWindow->shouldClose())
        {
            nativeSystem->quitRequested = true;
        }

        for (const std::unique_ptr<Window> &window : nativeSystem->windows)
        {
            Window *windowPtr = window.get();
            if (windowPtr != nativeSystem->mainWindow && windowPtr->shouldClose())
            {
                if (std::find(nativeSystem->pendingDestroy.begin(), nativeSystem->pendingDestroy.end(), windowPtr) == nativeSystem->pendingDestroy.end())
                {
                    nativeSystem->pendingDestroy.push_back(windowPtr);
                }
            }
        }
    }

    bool WindowSystem::shouldQuit() const
    {
        return nativeSystem == nullptr || nativeSystem->quitRequested || (nativeSystem->mainWindow != nullptr && nativeSystem->mainWindow->shouldClose());
    }

    Window *WindowSystem::getMainWindow() const
    {
        return nativeSystem != nullptr ? nativeSystem->mainWindow : nullptr;
    }

    Window *WindowSystem::getFocusedWindow() const
    {
        return nativeSystem != nullptr ? nativeSystem->focusedWindow : nullptr;
    }

    Window *WindowSystem::getToolInputWindow() const
    {
        return nativeSystem != nullptr ? nativeSystem->toolInputWindow : nullptr;
    }

    void WindowSystem::getWindows(std::vector<Window *> &outWindows) const
    {
        outWindows.clear();

        if (nativeSystem == nullptr)
        {
            return;
        }

        outWindows.reserve(nativeSystem->windows.size());
        for (const std::unique_ptr<Window> &window : nativeSystem->windows)
        {
            outWindows.push_back(window.get());
        }
    }
}
