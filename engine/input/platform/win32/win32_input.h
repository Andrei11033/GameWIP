/// @file win32_input.h
/// @brief Private Win32 message and gamepad input backend interface.

#pragma once

#include "input/input.h"

#ifndef INPUT_INTERNAL_TEST_HOOKS
#define INPUT_INTERNAL_TEST_HOOKS 0
#endif

#if INPUT_INTERNAL_TEST_HOOKS
#include <string>
#include <string_view>
#endif

namespace GameWIP::Input::Platform::Win32
{
#if INPUT_INTERNAL_TEST_HOOKS
    namespace TestHooks
    {
        /// @brief Converts Win32 UTF-16 metadata using the production Unicode authority.
        /// @warning Test-only, source-tree API; malformed input returns an empty string.
        [[nodiscard]] std::string convertDeviceMetadata(std::wstring_view text);

        /// @brief Applies production HID normalization to one synthetic native value.
        /// @warning Test-only, source-tree API; invalid ranges return zero.
        [[nodiscard]] float normalizeHidValue(long value, long logicalMinimum, long logicalMaximum, InputDeviceType deviceType, unsigned short usage);
    } // namespace TestHooks
#endif

    /// @brief Handles the Win32 input path used by the main input state.
    bool handleMessage(unsigned int message, unsigned long long wParam, long long lParam, InputState &inputState, InputDeviceRegistry &devices);

    /// @brief Handles the UI-message subset used by tool windows.
    bool handleUiMessage(unsigned int message, unsigned long long wParam, long long lParam, InputState &inputState);

    /// @brief Polls gamepads that are not driven by the Win32 message queue.
    void updateGamepads(InputState &inputState, InputDeviceRegistry &devices);

    /// @brief Registers raw-input devices and reports the native error on failure.
    bool registerInputDevices(void *windowHandle, InputDeviceRegistry &devices, unsigned long &win32Error);
} // namespace GameWIP::Input::Platform::Win32
