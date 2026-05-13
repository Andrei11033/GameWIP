#include "win32_input.h"
#include "input/internal/input_state_access.h"

#include <windows.h>
#include <windowsx.h>
#include <xinput.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <string_view>
#include <vector>

namespace
{
    namespace InputInternal = GameWIP::Input::Internal;
    namespace KeyboardControlCode = GameWIP::Input::KeyboardControlCode;
    using GameWIP::Input::ControlCode;
    using GameWIP::Input::DeviceIndex;
    using GameWIP::Input::GamepadAxis;
    using GameWIP::Input::GamepadButton;
    using GameWIP::Input::InputDeviceType;
    using GameWIP::Input::InputState;
    using GameWIP::Input::makeGamepadAxis;
    using GameWIP::Input::makeGamepadButton;
    using GameWIP::Input::makeKeyboardKey;
    using GameWIP::Input::makeMouseButton;
    using GameWIP::Input::makeMouseWheel;
    using GameWIP::Input::MouseButton;
    using GameWIP::Input::MouseWheel;

    constexpr DWORD maxGamepadCount = 4;
    constexpr auto disconnectedGamepadPollInterval = std::chrono::seconds(1);

    using XInputGetStateFn = DWORD(WINAPI *)(DWORD, XINPUT_STATE *);

    XInputGetStateFn cachedXInputGetState = nullptr;
    bool attemptedXInputLoad = false;
    std::array<DWORD, maxGamepadCount> cachedGamepadPacketNumbers{};
    std::array<std::uint64_t, maxGamepadCount> cachedGamepadClearGenerations{};
    std::array<const InputState *, maxGamepadCount> cachedGamepadInputStates{};
    std::array<bool, maxGamepadCount> cachedGamepadConnected{};
    std::array<bool, maxGamepadCount> hasCachedGamepadPacket{};
    std::array<bool, maxGamepadCount> gamepadControlsCleared{true, true, true, true};
    std::chrono::steady_clock::time_point nextDisconnectedGamepadPollTime{};
    DWORD nextDisconnectedGamepadSlotToPoll = 0;

    constexpr std::array<GamepadButton, 15> allGamepadButtons{
        GamepadButton::North,
        GamepadButton::South,
        GamepadButton::East,
        GamepadButton::West,
        GamepadButton::DpadUp,
        GamepadButton::DpadDown,
        GamepadButton::DpadLeft,
        GamepadButton::DpadRight,
        GamepadButton::LeftShoulder,
        GamepadButton::RightShoulder,
        GamepadButton::Back,
        GamepadButton::Start,
        GamepadButton::Guide,
        GamepadButton::LeftStick,
        GamepadButton::RightStick};

    constexpr std::array<GamepadAxis, 6> allGamepadAxes{
        GamepadAxis::LeftX,
        GamepadAxis::LeftY,
        GamepadAxis::RightX,
        GamepadAxis::RightY,
        GamepadAxis::LeftTrigger,
        GamepadAxis::RightTrigger};

    XInputGetStateFn getXInputGetState()
    {
        if (attemptedXInputLoad)
        {
            return cachedXInputGetState;
        }
        attemptedXInputLoad = true;

        constexpr std::array<const wchar_t *, 3> xInputLibraries{
            L"xinput1_4.dll",
            L"xinput1_3.dll",
            L"xinput9_1_0.dll"};

        for (const wchar_t *libraryName : xInputLibraries)
        {
            HMODULE library = LoadLibraryW(libraryName);
            if (library == nullptr)
            {
                continue;
            }

            FARPROC function = GetProcAddress(library, "XInputGetState");
            if (function != nullptr)
            {
                cachedXInputGetState = reinterpret_cast<XInputGetStateFn>(function);
                return cachedXInputGetState;
            }

            FreeLibrary(library);
        }

        return nullptr;
    }

    float normalizeTrigger(BYTE value)
    {
        return static_cast<float>(value) / 255.0f;
    }

    float normalizeThumbAxis(SHORT value)
    {
        if (value < 0)
        {
            return static_cast<float>(value) / 32768.0f;
        }

        return static_cast<float>(value) / 32767.0f;
    }

    void clearGamepadControls(InputState &inputState, DeviceIndex deviceIndex)
    {
        for (GamepadButton button : allGamepadButtons)
        {
            InputInternal::InputStateAccess::setButton(inputState, makeGamepadButton(deviceIndex, button), false);
        }

        for (GamepadAxis axis : allGamepadAxes)
        {
            InputInternal::InputStateAccess::setAxis(inputState, makeGamepadAxis(deviceIndex, axis), 0.0f);
        }
    }

    DWORD chooseDisconnectedGamepadSlot()
    {
        for (DWORD attempt = 0; attempt < maxGamepadCount; ++attempt)
        {
            DWORD userIndex = (nextDisconnectedGamepadSlotToPoll + attempt) % maxGamepadCount;
            if (!cachedGamepadConnected[static_cast<std::size_t>(userIndex)])
            {
                nextDisconnectedGamepadSlotToPoll = (userIndex + 1) % maxGamepadCount;
                return userIndex;
            }
        }

        return maxGamepadCount;
    }

    void markGamepadDisconnected(InputState &inputState, DeviceIndex deviceIndex, std::size_t cacheIndex, std::uint64_t clearGeneration)
    {
        if (!gamepadControlsCleared[cacheIndex])
        {
            clearGamepadControls(inputState, deviceIndex);
            gamepadControlsCleared[cacheIndex] = true;
        }

        InputInternal::InputStateAccess::setDeviceConnected(inputState, InputDeviceType::Gamepad, deviceIndex, false);
        cachedGamepadInputStates[cacheIndex] = &inputState;
        cachedGamepadClearGenerations[cacheIndex] = clearGeneration;
        cachedGamepadConnected[cacheIndex] = false;
        hasCachedGamepadPacket[cacheIndex] = false;
    }

    void markXInputUnavailable(InputState &inputState, std::uint64_t clearGeneration)
    {
        for (DWORD userIndex = 0; userIndex < maxGamepadCount; ++userIndex)
        {
            std::size_t cacheIndex = static_cast<std::size_t>(userIndex);
            if (cachedGamepadConnected[cacheIndex] || !gamepadControlsCleared[cacheIndex])
            {
                DeviceIndex deviceIndex = static_cast<DeviceIndex>(userIndex);
                markGamepadDisconnected(inputState, deviceIndex, cacheIndex, clearGeneration);
            }
            else
            {
                cachedGamepadInputStates[cacheIndex] = nullptr;
                hasCachedGamepadPacket[cacheIndex] = false;
            }
        }
    }

    void feedGamepadButton(InputState &inputState, DeviceIndex deviceIndex, WORD buttons, WORD mask, GamepadButton button)
    {
        InputInternal::InputStateAccess::setButton(inputState, makeGamepadButton(deviceIndex, button), (buttons & mask) != 0);
    }

    void feedGamepadAxis(InputState &inputState, DeviceIndex deviceIndex, GamepadAxis axis, float value)
    {
        InputInternal::InputStateAccess::setAxis(inputState, makeGamepadAxis(deviceIndex, axis), value);
    }

    void feedGamepadState(InputState &inputState, DeviceIndex deviceIndex, const XINPUT_STATE &state)
    {
        WORD buttons = state.Gamepad.wButtons;

        feedGamepadButton(inputState, deviceIndex, buttons, XINPUT_GAMEPAD_Y, GamepadButton::North);
        feedGamepadButton(inputState, deviceIndex, buttons, XINPUT_GAMEPAD_A, GamepadButton::South);
        feedGamepadButton(inputState, deviceIndex, buttons, XINPUT_GAMEPAD_B, GamepadButton::East);
        feedGamepadButton(inputState, deviceIndex, buttons, XINPUT_GAMEPAD_X, GamepadButton::West);
        feedGamepadButton(inputState, deviceIndex, buttons, XINPUT_GAMEPAD_DPAD_UP, GamepadButton::DpadUp);
        feedGamepadButton(inputState, deviceIndex, buttons, XINPUT_GAMEPAD_DPAD_DOWN, GamepadButton::DpadDown);
        feedGamepadButton(inputState, deviceIndex, buttons, XINPUT_GAMEPAD_DPAD_LEFT, GamepadButton::DpadLeft);
        feedGamepadButton(inputState, deviceIndex, buttons, XINPUT_GAMEPAD_DPAD_RIGHT, GamepadButton::DpadRight);
        feedGamepadButton(inputState, deviceIndex, buttons, XINPUT_GAMEPAD_LEFT_SHOULDER, GamepadButton::LeftShoulder);
        feedGamepadButton(inputState, deviceIndex, buttons, XINPUT_GAMEPAD_RIGHT_SHOULDER, GamepadButton::RightShoulder);
        feedGamepadButton(inputState, deviceIndex, buttons, XINPUT_GAMEPAD_BACK, GamepadButton::Back);
        feedGamepadButton(inputState, deviceIndex, buttons, XINPUT_GAMEPAD_START, GamepadButton::Start);
        feedGamepadButton(inputState, deviceIndex, buttons, XINPUT_GAMEPAD_LEFT_THUMB, GamepadButton::LeftStick);
        feedGamepadButton(inputState, deviceIndex, buttons, XINPUT_GAMEPAD_RIGHT_THUMB, GamepadButton::RightStick);

        // Standard XInput does not expose the Guide button through XInputGetState.
        InputInternal::InputStateAccess::setButton(inputState, makeGamepadButton(deviceIndex, GamepadButton::Guide), false);

        feedGamepadAxis(inputState, deviceIndex, GamepadAxis::LeftX, normalizeThumbAxis(state.Gamepad.sThumbLX));
        feedGamepadAxis(inputState, deviceIndex, GamepadAxis::LeftY, normalizeThumbAxis(state.Gamepad.sThumbLY));
        feedGamepadAxis(inputState, deviceIndex, GamepadAxis::RightX, normalizeThumbAxis(state.Gamepad.sThumbRX));
        feedGamepadAxis(inputState, deviceIndex, GamepadAxis::RightY, normalizeThumbAxis(state.Gamepad.sThumbRY));
        feedGamepadAxis(inputState, deviceIndex, GamepadAxis::LeftTrigger, normalizeTrigger(state.Gamepad.bLeftTrigger));
        feedGamepadAxis(inputState, deviceIndex, GamepadAxis::RightTrigger, normalizeTrigger(state.Gamepad.bRightTrigger));
    }

    /// @brief Returns whether a UTF-16 code unit is a high surrogate.
    bool isHighSurrogate(char16_t codeUnit)
    {
        return codeUnit >= 0xD800 && codeUnit <= 0xDBFF;
    }

    /// @brief Returns whether a UTF-16 code unit is a low surrogate.
    bool isLowSurrogate(char16_t codeUnit)
    {
        return codeUnit >= 0xDC00 && codeUnit <= 0xDFFF;
    }

    /// @brief Returns whether a codepoint should be treated as typed text.
    bool isTextCodepoint(char32_t codepoint)
    {
        return codepoint >= 0x20 && codepoint != 0x7F;
    }

    /// @brief Combines a UTF-16 surrogate pair into one Unicode codepoint.
    char32_t combineSurrogates(char16_t highSurrogate, char16_t lowSurrogate)
    {
        return 0x10000 +
               ((static_cast<char32_t>(highSurrogate) - 0xD800) << 10) +
               (static_cast<char32_t>(lowSurrogate) - 0xDC00);
    }

    /// @brief Feeds one UTF-16 code unit from WM_CHAR into text input.
    /// @param codeUnit UTF-16 code unit from Win32.
    /// @param inputState Input state to update.
    /// @return True if the message was consumed.
    bool feedUtf16TextCodeUnit(char16_t codeUnit, InputState &inputState)
    {
        char16_t pendingHighSurrogate = InputInternal::InputStateAccess::getPendingTextHighSurrogate(inputState);

        if (isHighSurrogate(codeUnit))
        {
            if (pendingHighSurrogate != 0)
            {
                InputInternal::InputStateAccess::addTextCodepoint(inputState, 0xFFFD);
            }
            InputInternal::InputStateAccess::setPendingTextHighSurrogate(inputState, codeUnit);
            return true;
        }

        if (isLowSurrogate(codeUnit))
        {
            if (pendingHighSurrogate != 0)
            {
                char32_t codepoint = combineSurrogates(pendingHighSurrogate, codeUnit);
                InputInternal::InputStateAccess::clearTextComposition(inputState);
                InputInternal::InputStateAccess::addTextCodepoint(inputState, codepoint);
            }
            else
            {
                InputInternal::InputStateAccess::addTextCodepoint(inputState, 0xFFFD);
            }
            return true;
        }

        if (pendingHighSurrogate != 0)
        {
            InputInternal::InputStateAccess::addTextCodepoint(inputState, 0xFFFD);
            InputInternal::InputStateAccess::clearTextComposition(inputState);
        }

        char32_t codepoint = static_cast<char32_t>(codeUnit);
        if (!isTextCodepoint(codepoint))
        {
            return true;
        }

        InputInternal::InputStateAccess::addTextCodepoint(inputState, codepoint);
        return true;
    }

    /// @brief Feeds one UTF-32 codepoint from WM_UNICHAR into text input.
    /// @param codepoint Unicode codepoint from Win32.
    /// @param inputState Input state to update.
    /// @return True if the message was consumed.
    bool feedUnicodeTextCodepoint(char32_t codepoint, InputState &inputState)
    {
        InputInternal::InputStateAccess::clearTextComposition(inputState);
        if (!isTextCodepoint(codepoint))
        {
            return true;
        }

        InputInternal::InputStateAccess::addTextCodepoint(inputState, codepoint);
        return true;
    }

    /// @brief Converts a Win32 set-1 scan code into a USB HID keyboard usage ID.
    /// @param scanCode Low-byte Win32 scan code.
    /// @param extendedE0 True for E0-prefixed scan codes.
    /// @param extendedE1 True for E1-prefixed scan codes.
    /// @param virtualKey Win32 virtual-key value from the raw keyboard packet.
    /// @return USB HID usage ID, or 0 if unsupported.
    ControlCode translateWin32ScanCodeToKeyboardControlCode(ControlCode scanCode, bool extendedE0, bool extendedE1, USHORT virtualKey)
    {
        if (virtualKey == VK_PAUSE || extendedE1)
        {
            return KeyboardControlCode::Pause;
        }

        if (extendedE0)
        {
            switch (scanCode)
            {
            case 0x1C:
                return KeyboardControlCode::KeypadEnter;
            case 0x1D:
                return KeyboardControlCode::RightControl;
            case 0x2A:
            case 0x36:
                return 0; // Fake shift packets used by some extended-key sequences.
            case 0x35:
                return KeyboardControlCode::KeypadDivide;
            case 0x37:
                return KeyboardControlCode::PrintScreen;
            case 0x38:
                return KeyboardControlCode::RightAlt;
            case 0x47:
                return KeyboardControlCode::Home;
            case 0x48:
                return KeyboardControlCode::UpArrow;
            case 0x49:
                return KeyboardControlCode::PageUp;
            case 0x4B:
                return KeyboardControlCode::LeftArrow;
            case 0x4D:
                return KeyboardControlCode::RightArrow;
            case 0x4F:
                return KeyboardControlCode::End;
            case 0x50:
                return KeyboardControlCode::DownArrow;
            case 0x51:
                return KeyboardControlCode::PageDown;
            case 0x52:
                return KeyboardControlCode::Insert;
            case 0x53:
                return KeyboardControlCode::Delete;
            case 0x5B:
                return KeyboardControlCode::LeftSuper;
            case 0x5C:
                return KeyboardControlCode::RightSuper;
            case 0x5D:
                return KeyboardControlCode::Application;
            default:
                return 0;
            }
        }

        switch (scanCode)
        {
        case 0x01:
            return KeyboardControlCode::Escape;
        case 0x02:
            return KeyboardControlCode::Digit1;
        case 0x03:
            return KeyboardControlCode::Digit2;
        case 0x04:
            return KeyboardControlCode::Digit3;
        case 0x05:
            return KeyboardControlCode::Digit4;
        case 0x06:
            return KeyboardControlCode::Digit5;
        case 0x07:
            return KeyboardControlCode::Digit6;
        case 0x08:
            return KeyboardControlCode::Digit7;
        case 0x09:
            return KeyboardControlCode::Digit8;
        case 0x0A:
            return KeyboardControlCode::Digit9;
        case 0x0B:
            return KeyboardControlCode::Digit0;
        case 0x0C:
            return KeyboardControlCode::Minus;
        case 0x0D:
            return KeyboardControlCode::Equal;
        case 0x0E:
            return KeyboardControlCode::Backspace;
        case 0x0F:
            return KeyboardControlCode::Tab;
        case 0x10:
            return KeyboardControlCode::Q;
        case 0x11:
            return KeyboardControlCode::W;
        case 0x12:
            return KeyboardControlCode::E;
        case 0x13:
            return KeyboardControlCode::R;
        case 0x14:
            return KeyboardControlCode::T;
        case 0x15:
            return KeyboardControlCode::Y;
        case 0x16:
            return KeyboardControlCode::U;
        case 0x17:
            return KeyboardControlCode::I;
        case 0x18:
            return KeyboardControlCode::O;
        case 0x19:
            return KeyboardControlCode::P;
        case 0x1A:
            return KeyboardControlCode::LeftBracket;
        case 0x1B:
            return KeyboardControlCode::RightBracket;
        case 0x1C:
            return KeyboardControlCode::Enter;
        case 0x1D:
            return KeyboardControlCode::LeftControl;
        case 0x1E:
            return KeyboardControlCode::A;
        case 0x1F:
            return KeyboardControlCode::S;
        case 0x20:
            return KeyboardControlCode::D;
        case 0x21:
            return KeyboardControlCode::F;
        case 0x22:
            return KeyboardControlCode::G;
        case 0x23:
            return KeyboardControlCode::H;
        case 0x24:
            return KeyboardControlCode::J;
        case 0x25:
            return KeyboardControlCode::K;
        case 0x26:
            return KeyboardControlCode::L;
        case 0x27:
            return KeyboardControlCode::Semicolon;
        case 0x28:
            return KeyboardControlCode::Apostrophe;
        case 0x29:
            return KeyboardControlCode::Grave;
        case 0x2A:
            return KeyboardControlCode::LeftShift;
        case 0x2B:
            return KeyboardControlCode::Backslash;
        case 0x2C:
            return KeyboardControlCode::Z;
        case 0x2D:
            return KeyboardControlCode::X;
        case 0x2E:
            return KeyboardControlCode::C;
        case 0x2F:
            return KeyboardControlCode::V;
        case 0x30:
            return KeyboardControlCode::B;
        case 0x31:
            return KeyboardControlCode::N;
        case 0x32:
            return KeyboardControlCode::M;
        case 0x33:
            return KeyboardControlCode::Comma;
        case 0x34:
            return KeyboardControlCode::Period;
        case 0x35:
            return KeyboardControlCode::Slash;
        case 0x36:
            return KeyboardControlCode::RightShift;
        case 0x37:
            return KeyboardControlCode::KeypadMultiply;
        case 0x38:
            return KeyboardControlCode::LeftAlt;
        case 0x39:
            return KeyboardControlCode::Space;
        case 0x3A:
            return KeyboardControlCode::CapsLock;
        case 0x3B:
            return KeyboardControlCode::F1;
        case 0x3C:
            return KeyboardControlCode::F2;
        case 0x3D:
            return KeyboardControlCode::F3;
        case 0x3E:
            return KeyboardControlCode::F4;
        case 0x3F:
            return KeyboardControlCode::F5;
        case 0x40:
            return KeyboardControlCode::F6;
        case 0x41:
            return KeyboardControlCode::F7;
        case 0x42:
            return KeyboardControlCode::F8;
        case 0x43:
            return KeyboardControlCode::F9;
        case 0x44:
            return KeyboardControlCode::F10;
        case 0x45:
            return KeyboardControlCode::NumLock;
        case 0x46:
            return KeyboardControlCode::ScrollLock;
        case 0x47:
            return KeyboardControlCode::Keypad7;
        case 0x48:
            return KeyboardControlCode::Keypad8;
        case 0x49:
            return KeyboardControlCode::Keypad9;
        case 0x4A:
            return KeyboardControlCode::KeypadMinus;
        case 0x4B:
            return KeyboardControlCode::Keypad4;
        case 0x4C:
            return KeyboardControlCode::Keypad5;
        case 0x4D:
            return KeyboardControlCode::Keypad6;
        case 0x4E:
            return KeyboardControlCode::KeypadPlus;
        case 0x4F:
            return KeyboardControlCode::Keypad1;
        case 0x50:
            return KeyboardControlCode::Keypad2;
        case 0x51:
            return KeyboardControlCode::Keypad3;
        case 0x52:
            return KeyboardControlCode::Keypad0;
        case 0x53:
            return KeyboardControlCode::KeypadDecimal;
        case 0x56:
            return KeyboardControlCode::NonUsBackslash;
        case 0x57:
            return KeyboardControlCode::F11;
        case 0x58:
            return KeyboardControlCode::F12;
        default:
            return virtualKey == VK_SNAPSHOT ? KeyboardControlCode::PrintScreen : 0;
        }
    }

    /// @brief Builds the physical keyboard control code used by the generic input API.
    /// @param rawKeyboard Raw keyboard packet from Win32.
    /// @return USB HID keyboard usage ID, or 0 if no usable code exists.
    ControlCode getKeyboardControlCode(const RAWKEYBOARD &rawKeyboard)
    {
        ControlCode scanCode = rawKeyboard.MakeCode & 0xFF;
        bool extendedE0 = (rawKeyboard.Flags & RI_KEY_E0) != 0;
        bool extendedE1 = (rawKeyboard.Flags & RI_KEY_E1) != 0;

        if (scanCode == 0 && rawKeyboard.VKey != 0)
        {
            UINT mappedScanCode = MapVirtualKeyW(rawKeyboard.VKey, MAPVK_VK_TO_VSC_EX);
            if (mappedScanCode == 0)
            {
                return 0;
            }

            if ((mappedScanCode & 0xFF00) == 0xE000)
            {
                extendedE0 = true;
            }
            else if ((mappedScanCode & 0xFF00) == 0xE100)
            {
                extendedE1 = true;
            }

            scanCode = mappedScanCode & 0xFF;
        }

        return translateWin32ScanCodeToKeyboardControlCode(scanCode, extendedE0, extendedE1, rawKeyboard.VKey);
    }

    /// @brief Feeds one raw mouse button transition when the matching flag is present.
    /// @param rawMouse Raw mouse packet from Win32.
    /// @param downFlag Raw Input flag for button down.
    /// @param upFlag Raw Input flag for button up.
    /// @param button Engine mouse button to update.
    /// @param inputState Input state to update.
    /// @return True if the button was updated.
    bool feedMouseButton(const RAWMOUSE &rawMouse, USHORT downFlag, USHORT upFlag, MouseButton button, InputState &inputState)
    {
        bool updated = false;
        if ((rawMouse.usButtonFlags & downFlag) != 0)
        {
            InputInternal::InputStateAccess::setButton(inputState, makeMouseButton(button), true);
            updated = true;
        }

        if ((rawMouse.usButtonFlags & upFlag) != 0)
        {
            InputInternal::InputStateAccess::setButton(inputState, makeMouseButton(button), false);
            updated = true;
        }

        return updated;
    }

    /// @brief Handles a raw mouse packet.
    /// @param rawMouse Raw mouse packet from Win32.
    /// @param inputState Input state to update.
    /// @return True when a mouse packet was handled.
    bool handleRawMouseInput(const RAWMOUSE &rawMouse, InputState &inputState)
    {
        feedMouseButton(rawMouse, RI_MOUSE_LEFT_BUTTON_DOWN, RI_MOUSE_LEFT_BUTTON_UP, MouseButton::Left, inputState);
        feedMouseButton(rawMouse, RI_MOUSE_RIGHT_BUTTON_DOWN, RI_MOUSE_RIGHT_BUTTON_UP, MouseButton::Right, inputState);
        feedMouseButton(rawMouse, RI_MOUSE_MIDDLE_BUTTON_DOWN, RI_MOUSE_MIDDLE_BUTTON_UP, MouseButton::Middle, inputState);
        feedMouseButton(rawMouse, RI_MOUSE_BUTTON_4_DOWN, RI_MOUSE_BUTTON_4_UP, MouseButton::X1, inputState);
        feedMouseButton(rawMouse, RI_MOUSE_BUTTON_5_DOWN, RI_MOUSE_BUTTON_5_UP, MouseButton::X2, inputState);

        if ((rawMouse.usFlags & MOUSE_MOVE_ABSOLUTE) == 0 && (rawMouse.lLastX != 0 || rawMouse.lLastY != 0))
        {
            InputInternal::InputStateAccess::addMouseDelta(inputState, rawMouse.lLastX, rawMouse.lLastY);
        }

        if ((rawMouse.usButtonFlags & RI_MOUSE_WHEEL) != 0)
        {
            float wheelAmount = static_cast<float>(static_cast<SHORT>(rawMouse.usButtonData)) / static_cast<float>(WHEEL_DELTA);
            InputInternal::InputStateAccess::addWheelDelta(inputState, makeMouseWheel(MouseWheel::Vertical), wheelAmount);
        }

        if ((rawMouse.usButtonFlags & RI_MOUSE_HWHEEL) != 0)
        {
            float wheelAmount = static_cast<float>(static_cast<SHORT>(rawMouse.usButtonData)) / static_cast<float>(WHEEL_DELTA);
            InputInternal::InputStateAccess::addWheelDelta(inputState, makeMouseWheel(MouseWheel::Horizontal), wheelAmount);
        }

        return true;
    }

    /// @brief Extracts a signed client coordinate from a mouse message LPARAM.
    int getSignedLowWord(LPARAM lParam)
    {
        return static_cast<int>(static_cast<short>(LOWORD(lParam)));
    }

    /// @brief Extracts a signed client coordinate from a mouse message LPARAM.
    int getSignedHighWord(LPARAM lParam)
    {
        return static_cast<int>(static_cast<short>(HIWORD(lParam)));
    }

    bool feedUiMouseButton(InputState &inputState, MouseButton button, bool isDown)
    {
        InputInternal::InputStateAccess::setButton(inputState, makeMouseButton(button), isDown);
        return true;
    }

    bool feedUiMouseWheel(InputState &inputState, MouseWheel wheel, unsigned long long wParam)
    {
        float wheelAmount = static_cast<float>(GET_WHEEL_DELTA_WPARAM(static_cast<WPARAM>(wParam))) / static_cast<float>(WHEEL_DELTA);
        InputInternal::InputStateAccess::addWheelDelta(inputState, makeMouseWheel(wheel), wheelAmount);
        return true;
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

        thread_local std::vector<unsigned char> buffer;
        buffer.resize(bufferSize);
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
            ControlCode controlCode = getKeyboardControlCode(rawKeyboard);
            if (controlCode != 0)
            {
                bool isDown = (rawKeyboard.Flags & RI_KEY_BREAK) == 0;
                InputInternal::InputStateAccess::setButton(inputState, makeKeyboardKey(controlCode), isDown);
                return true;
            }
        }

        if (rawInput->header.dwType == RIM_TYPEMOUSE)
        {
            return handleRawMouseInput(rawInput->data.mouse, inputState);
        }

        return false;
    }
}

namespace GameWIP::Input::Platform::Win32
{
    void updateGamepads(InputState &inputState)
    {
        XInputGetStateFn xInputGetState = getXInputGetState();
        std::uint64_t clearGeneration = InputInternal::InputStateAccess::getClearGeneration(inputState);

        if (xInputGetState == nullptr)
        {
            markXInputUnavailable(inputState, clearGeneration);
            return;
        }

        DWORD disconnectedSlotToPoll = maxGamepadCount;
        auto now = std::chrono::steady_clock::now();
        if (now >= nextDisconnectedGamepadPollTime)
        {
            disconnectedSlotToPoll = chooseDisconnectedGamepadSlot();
            if (disconnectedSlotToPoll != maxGamepadCount)
            {
                nextDisconnectedGamepadPollTime = now + disconnectedGamepadPollInterval;
            }
        }

        for (DWORD userIndex = 0; userIndex < maxGamepadCount; ++userIndex)
        {
            DeviceIndex deviceIndex = static_cast<DeviceIndex>(userIndex);
            std::size_t cacheIndex = static_cast<std::size_t>(userIndex);

            bool wasConnected = cachedGamepadConnected[cacheIndex];
            if (!wasConnected && userIndex != disconnectedSlotToPoll)
            {
                continue;
            }

            XINPUT_STATE state{};
            DWORD result = xInputGetState(userIndex, &state);
            if (result == ERROR_SUCCESS)
            {
                InputInternal::InputStateAccess::setDeviceConnected(inputState, InputDeviceType::Gamepad, deviceIndex, true);
                cachedGamepadConnected[cacheIndex] = true;

                if (hasCachedGamepadPacket[cacheIndex] &&
                    cachedGamepadInputStates[cacheIndex] == &inputState &&
                    cachedGamepadPacketNumbers[cacheIndex] == state.dwPacketNumber &&
                    cachedGamepadClearGenerations[cacheIndex] == clearGeneration)
                {
                    continue;
                }

                feedGamepadState(inputState, deviceIndex, state);
                cachedGamepadPacketNumbers[cacheIndex] = state.dwPacketNumber;
                cachedGamepadClearGenerations[cacheIndex] = clearGeneration;
                cachedGamepadInputStates[cacheIndex] = &inputState;
                hasCachedGamepadPacket[cacheIndex] = true;
                gamepadControlsCleared[cacheIndex] = false;
            }
            else
            {
                markGamepadDisconnected(inputState, deviceIndex, cacheIndex, clearGeneration);
            }
        }
    }

    bool handleUiMessage(unsigned int message, unsigned long long wParam, long long lParam, InputState &inputState)
    {
        switch (message)
        {
        case WM_MOUSEMOVE:
            InputInternal::InputStateAccess::setMousePosition(
                inputState,
                getSignedLowWord(lParam),
                getSignedHighWord(lParam));
            (void)wParam;
            return true;
        case WM_MOUSELEAVE:
            InputInternal::InputStateAccess::clearMousePosition(inputState);
            (void)wParam;
            return true;
        case WM_LBUTTONDOWN:
            return feedUiMouseButton(inputState, MouseButton::Left, true);
        case WM_LBUTTONUP:
            return feedUiMouseButton(inputState, MouseButton::Left, false);
        case WM_RBUTTONDOWN:
            return feedUiMouseButton(inputState, MouseButton::Right, true);
        case WM_RBUTTONUP:
            return feedUiMouseButton(inputState, MouseButton::Right, false);
        case WM_MBUTTONDOWN:
            return feedUiMouseButton(inputState, MouseButton::Middle, true);
        case WM_MBUTTONUP:
            return feedUiMouseButton(inputState, MouseButton::Middle, false);
        case WM_XBUTTONDOWN:
            return feedUiMouseButton(inputState, GET_XBUTTON_WPARAM(static_cast<WPARAM>(wParam)) == XBUTTON1 ? MouseButton::X1 : MouseButton::X2, true);
        case WM_XBUTTONUP:
            return feedUiMouseButton(inputState, GET_XBUTTON_WPARAM(static_cast<WPARAM>(wParam)) == XBUTTON1 ? MouseButton::X1 : MouseButton::X2, false);
        case WM_MOUSEWHEEL:
            return feedUiMouseWheel(inputState, MouseWheel::Vertical, wParam);
        case WM_MOUSEHWHEEL:
            return feedUiMouseWheel(inputState, MouseWheel::Horizontal, wParam);
        case WM_CHAR:
            feedUtf16TextCodeUnit(static_cast<char16_t>(wParam), inputState);
            return true;
        case WM_UNICHAR:
            if (wParam == UNICODE_NOCHAR)
            {
                return true;
            }

            feedUnicodeTextCodepoint(static_cast<char32_t>(wParam), inputState);
            return true;
        default:
            break;
        }

        (void)wParam;
        (void)lParam;
        return false;
    }

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

        return handleUiMessage(message, wParam, lParam, inputState);
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
