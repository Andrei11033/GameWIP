/// @file win32_input.h
/// @brief Private Win32 message and gamepad input backend interface.

#pragma once

#include "input/input.h"

namespace GameWIP::Input::Platform::Win32
{
    /// @brief Handles Win32 messages that feed the main input state.
    bool handleMessage(unsigned int message, unsigned long long wParam, long long lParam, InputState &inputState, InputDeviceRegistry &devices);

    /// @brief Handles the UI-message subset used by tool windows.
    bool handleUiMessage(unsigned int message, unsigned long long wParam, long long lParam, InputState &inputState);

    /// @brief Polls gamepads that are not driven by the Win32 message queue.
    void updateGamepads(InputState &inputState, InputDeviceRegistry &devices);

    /// @brief Registers raw-input devices and reports the native error on failure.
    bool registerInputDevices(void *windowHandle, InputDeviceRegistry &devices, unsigned long &win32Error);
} // namespace GameWIP::Input::Platform::Win32
