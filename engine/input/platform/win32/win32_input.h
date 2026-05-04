#pragma once

#include "input/input.h"

namespace GameWIP::Input::Platform::Win32
{
    /// @brief Handles a Windows input message and updates the input state accordingly.
    /// @param message The Windows message identifier.
    /// @param wParam The WPARAM parameter of the message, which typically contains additional information about the message.
    /// @param lParam The LPARAM parameter of the message, which typically contains additional information about the message.
    /// @param inputState The input state to update.
    /// @return true if the message was handled, false otherwise.
    bool handleMessage(unsigned int message, unsigned long long wParam, long long lParam, InputState &inputState);

    /// @brief Registers the raw input devices for the specified window.
    /// @param windowHandle The handle to the window for which to register raw input devices.
    /// @param win32Error A reference to a variable that will receive the Win32 error code if registration fails.
    /// @return true if the devices were registered successfully, false otherwise.
    bool registerInputDevices(void *windowHandle, unsigned long &win32Error);
}