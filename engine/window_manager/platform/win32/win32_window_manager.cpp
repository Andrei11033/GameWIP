#include "window_manager/window_manager.h"
#include "input/platform/win32/win32_input.h"

#include <windows.h>

#include <algorithm>
#include <memory>
#include <unordered_map>

namespace
{
    namespace Win32Input = GameWIP::Input::Platform::Win32;

    using Window = GameWIP::Window;
    using WindowDescription = GameWIP::Window::Description;
    using WindowInfo = GameWIP::Window::Info;
    using WindowResult = GameWIP::Window::Result;
    using WindowRole = GameWIP::Window::Role;
    using WindowManager = GameWIP::WindowManager;

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

namespace GameWIP
{
    struct WindowManager::NativeWindowManager
    {
        std::vector<std::unique_ptr<Window>> windows;
        std::unordered_map<HWND, Window *> windowsByHandle;
        std::vector<Window *> pendingDestroy;
        Window *mainWindow = nullptr;
        Window *focusedWindow = nullptr;
        Window *toolInputWindow = nullptr;
        Window *rawInputWindow = nullptr;
        bool rawInputRegistered = false;
        bool quitRequested = false;
    };

    WindowManager::WindowManager()
        : nativeManager(new NativeWindowManager)
    {
    }

    WindowManager::~WindowManager()
    {
        delete nativeManager;
        nativeManager = nullptr;
    }

    WindowResult WindowManager::createWindow(const WindowDescription &description, Window *&outWindow)
    {
        outWindow = nullptr;

        if (nativeManager == nullptr)
        {
            return WindowResult::NotCreated;
        }

        if (description.role == WindowRole::MainGame && nativeManager->mainWindow != nullptr)
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
            return WindowResult::PlatformCallFailed;
        }

        if (description.role == WindowRole::MainGame)
        {
            nativeManager->mainWindow = createdWindow;
            nativeManager->rawInputWindow = createdWindow;
            nativeManager->rawInputRegistered = false;
        }

        nativeManager->windowsByHandle.emplace(handle, createdWindow);
        nativeManager->windows.push_back(std::move(window));

        if (createdWindow->isFocused())
        {
            nativeManager->focusedWindow = createdWindow;
            nativeManager->toolInputWindow = createdWindow->getRole() == WindowRole::Tool ? createdWindow : nullptr;
        }

        outWindow = createdWindow;
        return WindowResult::Success;
    }

    WindowResult WindowManager::destroyWindow(Window &window)
    {
        if (nativeManager == nullptr)
        {
            return WindowResult::NotCreated;
        }

        auto windowIt = std::find_if(nativeManager->windows.begin(), nativeManager->windows.end(), [&window](const std::unique_ptr<Window> &candidate) {
            return candidate.get() == &window;
        });

        if (windowIt == nativeManager->windows.end())
        {
            return WindowResult::NotCreated;
        }

        Window *windowPtr = windowIt->get();
        HWND handle = toNativeHandle(*windowPtr);
        if (handle != nullptr)
        {
            nativeManager->windowsByHandle.erase(handle);
        }

        if (nativeManager->focusedWindow == windowPtr)
        {
            nativeManager->focusedWindow = nullptr;
        }

        if (nativeManager->toolInputWindow == windowPtr)
        {
            nativeManager->toolInputWindow = nullptr;
        }

        if (nativeManager->rawInputWindow == windowPtr)
        {
            nativeManager->rawInputWindow = nullptr;
            nativeManager->rawInputRegistered = false;
        }

        bool destroyingMainWindow = nativeManager->mainWindow == windowPtr;
        if (destroyingMainWindow)
        {
            nativeManager->mainWindow = nullptr;
            nativeManager->quitRequested = true;
        }

        nativeManager->pendingDestroy.erase(
            std::remove(nativeManager->pendingDestroy.begin(), nativeManager->pendingDestroy.end(), windowPtr),
            nativeManager->pendingDestroy.end());

        WindowResult destroyResult = windowPtr->destroy();
        nativeManager->windows.erase(windowIt);
        return destroyResult;
    }

    void WindowManager::pollEvents(Input::InputState &gameInput, Input::InputDeviceRegistry &inputDevices, Input::InputState *toolInput)
    {
        if (nativeManager == nullptr)
        {
            return;
        }

        if (!nativeManager->rawInputRegistered && nativeManager->rawInputWindow != nullptr)
        {
            unsigned long rawInputError = 0;
            HWND rawInputHandle = toNativeHandle(*nativeManager->rawInputWindow);
            if (!Win32Input::registerInputDevices(rawInputHandle, inputDevices, rawInputError))
            {
                nativeManager->quitRequested = true;
                (void)rawInputError;
                return;
            }

            nativeManager->rawInputRegistered = true;
        }

        std::vector<Window *> pendingDestroy = std::move(nativeManager->pendingDestroy);
        nativeManager->pendingDestroy.clear();
        for (Window *window : pendingDestroy)
        {
            if (window != nullptr && window != nativeManager->mainWindow && containsWindow(nativeManager->windows, window))
            {
                destroyWindow(*window);
            }
        }

        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
        {
            if (message.message == WM_QUIT)
            {
                nativeManager->quitRequested = true;
                continue;
            }

            Window *targetWindow = nullptr;
            auto targetIt = nativeManager->windowsByHandle.find(message.hwnd);
            if (targetIt != nativeManager->windowsByHandle.end())
            {
                targetWindow = targetIt->second;
            }

            const bool targetIsMain = targetWindow != nullptr && targetWindow == nativeManager->mainWindow;
            const bool targetWasFocusedTool = targetWindow != nullptr && targetWindow == nativeManager->toolInputWindow;
            const bool mainFocusLost = targetIsMain && message.message == WM_KILLFOCUS;
            const bool toolFocusLost = targetWasFocusedTool && message.message == WM_KILLFOCUS;

            if (message.message == WM_SETFOCUS && targetWindow != nullptr)
            {
                if (nativeManager->toolInputWindow != nullptr && nativeManager->toolInputWindow != targetWindow && toolInput != nullptr)
                {
                    toolInput->clear();
                }

                nativeManager->focusedWindow = targetWindow;
                nativeManager->toolInputWindow = targetWindow->getRole() == WindowRole::Tool ? targetWindow : nullptr;
            }

            if (message.message == WM_KILLFOCUS && targetWindow == nativeManager->focusedWindow)
            {
                nativeManager->focusedWindow = nullptr;
            }

            if (targetIsMain && targetWindow->isFocused())
            {
                Win32Input::handleMessage(
                    static_cast<unsigned int>(message.message),
                    static_cast<unsigned long long>(message.wParam),
                    static_cast<long long>(message.lParam),
                    gameInput,
                    inputDevices);
            }

            if (toolInput != nullptr && targetWindow != nullptr && targetWindow->getRole() == WindowRole::Tool && targetWindow->isFocused())
            {
                nativeManager->toolInputWindow = targetWindow;
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

                if (nativeManager->toolInputWindow == targetWindow)
                {
                    nativeManager->toolInputWindow = nullptr;
                }
            }
        }

        if (nativeManager->mainWindow != nullptr && nativeManager->mainWindow->isFocused())
        {
            Win32Input::updateGamepads(gameInput, inputDevices);
        }

        if (nativeManager->mainWindow != nullptr && nativeManager->mainWindow->shouldClose())
        {
            nativeManager->quitRequested = true;
        }

        for (const std::unique_ptr<Window> &window : nativeManager->windows)
        {
            Window *windowPtr = window.get();
            if (windowPtr != nativeManager->mainWindow && windowPtr->shouldClose())
            {
                if (std::find(nativeManager->pendingDestroy.begin(), nativeManager->pendingDestroy.end(), windowPtr) == nativeManager->pendingDestroy.end())
                {
                    nativeManager->pendingDestroy.push_back(windowPtr);
                }
            }
        }
    }

    bool WindowManager::shouldQuit() const
    {
        return nativeManager == nullptr || nativeManager->quitRequested || (nativeManager->mainWindow != nullptr && nativeManager->mainWindow->shouldClose());
    }

    Window *WindowManager::getMainWindow() const
    {
        return nativeManager != nullptr ? nativeManager->mainWindow : nullptr;
    }

    Window *WindowManager::getFocusedWindow() const
    {
        return nativeManager != nullptr ? nativeManager->focusedWindow : nullptr;
    }

    Window *WindowManager::getToolInputWindow() const
    {
        return nativeManager != nullptr ? nativeManager->toolInputWindow : nullptr;
    }

    void WindowManager::getWindows(std::vector<Window *> &outWindows) const
    {
        outWindows.clear();

        if (nativeManager == nullptr)
        {
            return;
        }

        outWindows.reserve(nativeManager->windows.size());
        for (const std::unique_ptr<Window> &window : nativeManager->windows)
        {
            outWindows.push_back(window.get());
        }
    }
}

