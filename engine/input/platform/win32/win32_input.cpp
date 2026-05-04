#include "win32_input.h"

#include <windows.h>
#include <vector>

namespace
{
    using GameWIP::Input::InputState;
    using GameWIP::Input::Key;

    /// @brief Tries to map a Windows virtual key to a GameWIP key.
    /// @param virtualKey The Windows virtual key to map.
    /// @param outKey The GameWIP key to set.
    /// @return True if the key was mapped, false otherwise.
    bool tryMapVirtualKey(WPARAM virtualKey, Key &outKey)
    {
        switch (virtualKey)
        {
        case VK_SPACE:
            outKey = Key::Space;
            return true;

        case VK_RETURN: // Enter key
            outKey = Key::Enter;
            return true;

        case VK_ESCAPE:
            outKey = Key::Escape;
            return true;

        case VK_UP:
            outKey = Key::UpArrow;
            return true;

        case VK_LEFT:
            outKey = Key::LeftArrow;
            return true;

        case VK_DOWN:
            outKey = Key::DownArrow;
            return true;

        case VK_RIGHT:
            outKey = Key::RightArrow;
            return true;

        case 'W':
            outKey = Key::W;
            return true;

        case 'A':
            outKey = Key::A;
            return true;

        case 'S':
            outKey = Key::S;
            return true;

        case 'D':
            outKey = Key::D;
            return true;

        case 'Q':
            outKey = Key::Q;
            return true;

        case 'E':
            outKey = Key::E;
            return true;
        default:
            return false;
        }
    }

    bool handleRawInput(LPARAM lParam, InputState &inputState)
    {
        HRAWINPUT rawInputHandle = reinterpret_cast<HRAWINPUT>(lParam);
        UINT bufferSize = 0;
        if (GetRawInputData(rawInputHandle, RID_INPUT, nullptr, &bufferSize, sizeof(RAWINPUTHEADER)) != 0)
        {
            return false;
        }
        if (bufferSize == 0)
        {
            return false;
        }

        std::vector<unsigned char> buffer(bufferSize);
        UINT expectedBufferSize = bufferSize;
        UINT result = GetRawInputData(rawInputHandle, RID_INPUT, buffer.data(), &bufferSize, sizeof(RAWINPUTHEADER));
        if (result == static_cast<UINT>(-1) || result != expectedBufferSize)
        {
            return false;
        }

        RAWINPUT *rawInput = reinterpret_cast<RAWINPUT *>(buffer.data());

        if (rawInput->header.dwType == RIM_TYPEKEYBOARD)
        {
            RAWKEYBOARD &rawKeyboard = rawInput->data.keyboard;
            Key key;
            if (tryMapVirtualKey(rawKeyboard.VKey, key))
            {
                bool isDown = (rawKeyboard.Flags & RI_KEY_BREAK) == 0;
                inputState.setKey(key, isDown);
                return true;
            }
        }

        return false;
    }
}

namespace GameWIP::Input::Platform::Win32
{
    bool handleMessage(unsigned int message, unsigned long long wParam, long long lParam, InputState &inputState)
    {

        if (message == WM_INPUT)
        {
            if (handleRawInput(lParam, inputState))
            {
                return true;
            }
            return false;
        }

        (void)wParam;
        return false;
    }

    bool registerInputDevices(void *windowHandle, unsigned long &win32Error)
    {
        win32Error = 0;

        if (windowHandle == nullptr)
        {
            win32Error = ERROR_INVALID_HANDLE;
            return false;
        }

        HWND hwnd = reinterpret_cast<HWND>(windowHandle);

        RAWINPUTDEVICE devices[2]{};
        // Mouse
        devices[0].usUsagePage = 0x01;
        devices[0].usUsage = 0x02;
        devices[0].dwFlags = 0;
        devices[0].hwndTarget = hwnd;

        // Keyboard
        devices[1].usUsagePage = 0x01;
        devices[1].usUsage = 0x06;
        devices[1].dwFlags = 0;
        devices[1].hwndTarget = hwnd;

        if (!RegisterRawInputDevices(devices, 2, sizeof(RAWINPUTDEVICE)))
        {
            win32Error = GetLastError();
            return false;
        }

        return true;
    }
}
