#pragma once

#include "input/input.h"

namespace GameWIP::Input::Platform::Win32
{
    /// @brief Handles a Windows input message.
    /// @param message Message identifier.
    /// @param wParam Message parameter (WPARAM).
    /// @param lParam Message parameter (LPARAM).
    /// @param inputState Input state to update.
    /// @return True if handled.
    bool handleMessage(unsigned int message, unsigned long long wParam, long long lParam, InputState &inputState, InputDeviceRegistry &devices);

    /// @brief Handles ordinary UI input messages for tool windows.
    /// @param message Message identifier.
    /// @param wParam Message parameter (WPARAM).
    /// @param lParam Message parameter (LPARAM).
    /// @param inputState Input state to update.
    /// @return True if handled.
    bool handleUiMessage(unsigned int message, unsigned long long wParam, long long lParam, InputState &inputState);

    /// @brief Polls gamepad devices that do not report through the Win32 message queue.
    /// @param inputState Input state to update.
    void updateGamepads(InputState &inputState, InputDeviceRegistry &devices);

    /// @brief Registers raw input devices.
    /// @param windowHandle Window handle.
    /// @param win32Error Win32 error if registration fails.
    /// @return True if registered.
    bool registerInputDevices(void *windowHandle, InputDeviceRegistry &devices, unsigned long &win32Error);
}
