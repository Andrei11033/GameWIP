#include "window.h"
#include "logger/logger.h"

#include <format>
#include <windows.h>

namespace
{
    using Window = GameWIP::Platform::Win32::Window;
    using GameWIP::Logger;
    using GameWIP::LogLevel;

    constexpr std::string_view windowLogSource = "Win32Window";

    LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        switch (message)
        {
        case WM_NCCREATE:
        {
            auto createStruct = reinterpret_cast<CREATESTRUCTA *>(lParam);
            auto window = reinterpret_cast<Window *>(createStruct->lpCreateParams);

            SetWindowLongPtrA(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(window));

            return DefWindowProcA(hwnd, message, wParam, lParam);
        }
        case WM_SIZE:
        {
            auto window = reinterpret_cast<Window *>(GetWindowLongPtrA(hwnd, GWLP_USERDATA));
            int width = LOWORD(lParam);
            int height = HIWORD(lParam);
            if (window != nullptr)
            {
                window->handleResize(width, height);
            }
            return 0;
        }
        case WM_CLOSE:
        {
            auto window = reinterpret_cast<Window *>(GetWindowLongPtrA(hwnd, GWLP_USERDATA));
            if (window != nullptr)
            {
                window->requestClose();
            }
            else
            {
                DestroyWindow(hwnd);
            }
            return 0;
        }
        case WM_DESTROY:
        {
            PostQuitMessage(0);
            return 0;
        }
        default:
            return DefWindowProcA(hwnd, message, wParam, lParam);
        }
    }
}

namespace GameWIP::Platform::Win32
{
    struct Window::NativeWindow
    {
        HWND handle = nullptr;
        bool closeRequested = false;
        HINSTANCE instance = nullptr;
        int clientWidth = 0;
        int clientHeight = 0;
    };

    void Window::destroy()
    {
        if (nativeWindow == nullptr)
        {
            return;
        }

        if (nativeWindow->handle != nullptr)
        {
            DestroyWindow(nativeWindow->handle);
            nativeWindow->handle = nullptr;
        }

        delete nativeWindow;
        nativeWindow = nullptr;
    }

    bool Window::shouldClose() const
    {
        if (nativeWindow == nullptr)
        {
            return true;
        }

        return nativeWindow->closeRequested;
    }

    void Window::requestClose()
    {
        if (nativeWindow != nullptr)
        {
            nativeWindow->closeRequested = true;
        }
    }

    Window::~Window()
    {
        destroy();
    }

    bool Window::create(const WindowDescription &description)
    {
        if (nativeWindow != nullptr)
        {
            destroy();
        }

        nativeWindow = new NativeWindow;

        nativeWindow->instance = GetModuleHandleA(nullptr);

        if (nativeWindow->instance == nullptr)
        {
            Logger::log(LogLevel::ERR, windowLogSource, "GetModuleHandleA failed during Win32 window creation.");
            destroy();
            return false;
        }

        const char *className = "GameWIPWindowClass";

        WNDCLASSA windowClass{};

        windowClass.lpfnWndProc = windowProc;
        windowClass.hInstance = nativeWindow->instance;
        windowClass.lpszClassName = className;

        if (RegisterClassA(&windowClass) == 0)
        {
            DWORD error = GetLastError();
            if (error != ERROR_CLASS_ALREADY_EXISTS)
            {
                Logger::log(LogLevel::ERR, windowLogSource, std::format("RegisterClassA failed with error {}.", error));
                destroy();
                return false;
            }
        }

        nativeWindow->handle = CreateWindowExA(0, className, description.title.c_str(), WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, description.width, description.height, nullptr, nullptr, nativeWindow->instance, this);

        if (nativeWindow->handle == nullptr)
        {
            Logger::log(LogLevel::ERR, windowLogSource, std::format("CreateWindowExA failed with error {}.", GetLastError()));
            destroy();
            return false;
        }

        RECT clientRect{};
        if (GetClientRect(nativeWindow->handle, &clientRect))
        {
            handleResize(clientRect.right - clientRect.left, clientRect.bottom - clientRect.top);
        }

        ShowWindow(nativeWindow->handle, SW_SHOW);
        UpdateWindow(nativeWindow->handle);
        Logger::log(LogLevel::INFO, windowLogSource, std::format("Created Win32 window '{}' with client size {}x{}.", description.title, nativeWindow->clientWidth, nativeWindow->clientHeight));

        return true;
    }

    void Window::pollEvents(Input::InputState &inputState)
    {
        MSG message{};

        while (PeekMessageA(&message, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&message);
            DispatchMessageA(&message);
        }

        (void)inputState;
    }

    int Window::getClientWidth() const
    {
        if (nativeWindow == nullptr)
        {
            return 0;
        }

        return nativeWindow->clientWidth;
    }

    int Window::getClientHeight() const
    {
        if (nativeWindow == nullptr)
        {
            return 0;
        }

        return nativeWindow->clientHeight;
    }

    void Window::handleResize(int width, int height)
    {
        if (nativeWindow == nullptr)
        {
            return;
        }

        if (width < 0)
        {
            width = 0;
        }
        if (height < 0)
        {
            height = 0;
        }

        nativeWindow->clientWidth = width;
        nativeWindow->clientHeight = height;
    }
}