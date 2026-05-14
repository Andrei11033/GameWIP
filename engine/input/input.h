#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace GameWIP::Input
{
    using DeviceIndex = std::uint8_t;  // Device slot within a device type.
    using ControlCode = std::uint32_t; // Backend-defined physical control code.

    /// @brief Broad family of input device.
    enum class InputDeviceType
    {
        Keyboard, // Keyboard device.
        Mouse,    // Mouse device.
        Gamepad   // Gamepad device.
    };

    /// @brief Kind of physical control being represented.
    enum class InputControlType
    {
        Button, // Binary button control.
        Axis,   // Continuous axis control.
        Wheel   // Relative wheel control.
    };

    /// @brief Physical input control identifier.
    struct InputControl
    {
        InputDeviceType deviceType = InputDeviceType::Keyboard;  // Device family.
        DeviceIndex deviceIndex = 0;                             // Device slot, mostly used for gamepads.
        InputControlType controlType = InputControlType::Button; // Button, axis, or wheel.
        ControlCode controlCode = 0;                             // Backend-defined physical control code.
    };

    /// @brief Type of input activation.
    enum class InputActivationType
    {
        ButtonPressed,  // Button transitioned to down.
        ButtonReleased, // Button transitioned to up.
        AxisPositive,   // Axis crossed from non-positive to positive.
        AxisNegative,   // Axis crossed from non-negative to negative.
        WheelPositive,  // Wheel moved in the positive direction.
        WheelNegative   // Wheel moved in the negative direction.
    };

    /// @brief Input change detected this frame.
    struct InputActivation
    {
        InputControl control{};                                                  // Control that changed.
        InputActivationType activationType = InputActivationType::ButtonPressed; // Kind of activation.
        float value = 0.0f;                                                      // Strength or amount of the activation.
    };

    /// @brief Standard mouse buttons.
    enum class MouseButton
    {
        Left,   // Primary mouse button.
        Right,  // Secondary mouse button.
        Middle, // Middle mouse button.
        X1,     // First extended mouse button.
        X2      // Second extended mouse button.
    };

    /// @brief Mouse wheel axes.
    enum class MouseWheel
    {
        Vertical,  // Vertical wheel axis.
        Horizontal // Horizontal wheel axis.
    };

    /// @brief Bindable raw mouse movement axes.
    enum class MouseAxis
    {
        DeltaX, // Raw mouse X movement.
        DeltaY  // Raw mouse Y movement.
    };

    /// @brief Common gamepad buttons.
    enum class GamepadButton
    {
        North,         // Top face button.
        South,         // Bottom face button.
        East,          // Right face button.
        West,          // Left face button.
        DpadUp,        // Directional pad up.
        DpadDown,      // Directional pad down.
        DpadLeft,      // Directional pad left.
        DpadRight,     // Directional pad right.
        LeftShoulder,  // Left shoulder button.
        RightShoulder, // Right shoulder button.
        Back,          // Back/select button.
        Start,         // Start/menu button.
        Guide,         // System guide button.
        LeftStick,     // Left stick press.
        RightStick     // Right stick press.
    };

    /// @brief Common gamepad axes.
    enum class GamepadAxis
    {
        LeftX,       // Left stick horizontal axis.
        LeftY,       // Left stick vertical axis.
        RightX,      // Right stick horizontal axis.
        RightY,      // Right stick vertical axis.
        LeftTrigger, // Left trigger axis.
        RightTrigger // Right trigger axis.
    };

    /// @brief Common keyboard control codes.
    /// @note Values use USB HID keyboard/keypad usage IDs, not platform scan codes.
    namespace KeyboardControlCode
    {
        inline constexpr ControlCode A = 0x04;
        inline constexpr ControlCode B = 0x05;
        inline constexpr ControlCode C = 0x06;
        inline constexpr ControlCode D = 0x07;
        inline constexpr ControlCode E = 0x08;
        inline constexpr ControlCode F = 0x09;
        inline constexpr ControlCode G = 0x0A;
        inline constexpr ControlCode H = 0x0B;
        inline constexpr ControlCode I = 0x0C;
        inline constexpr ControlCode J = 0x0D;
        inline constexpr ControlCode K = 0x0E;
        inline constexpr ControlCode L = 0x0F;
        inline constexpr ControlCode M = 0x10;
        inline constexpr ControlCode N = 0x11;
        inline constexpr ControlCode O = 0x12;
        inline constexpr ControlCode P = 0x13;
        inline constexpr ControlCode Q = 0x14;
        inline constexpr ControlCode R = 0x15;
        inline constexpr ControlCode S = 0x16;
        inline constexpr ControlCode T = 0x17;
        inline constexpr ControlCode U = 0x18;
        inline constexpr ControlCode V = 0x19;
        inline constexpr ControlCode W = 0x1A;
        inline constexpr ControlCode X = 0x1B;
        inline constexpr ControlCode Y = 0x1C;
        inline constexpr ControlCode Z = 0x1D;

        inline constexpr ControlCode Digit1 = 0x1E;
        inline constexpr ControlCode Digit2 = 0x1F;
        inline constexpr ControlCode Digit3 = 0x20;
        inline constexpr ControlCode Digit4 = 0x21;
        inline constexpr ControlCode Digit5 = 0x22;
        inline constexpr ControlCode Digit6 = 0x23;
        inline constexpr ControlCode Digit7 = 0x24;
        inline constexpr ControlCode Digit8 = 0x25;
        inline constexpr ControlCode Digit9 = 0x26;
        inline constexpr ControlCode Digit0 = 0x27;

        inline constexpr ControlCode Enter = 0x28;
        inline constexpr ControlCode Escape = 0x29;
        inline constexpr ControlCode Backspace = 0x2A;
        inline constexpr ControlCode Tab = 0x2B;
        inline constexpr ControlCode Space = 0x2C;
        inline constexpr ControlCode Minus = 0x2D;
        inline constexpr ControlCode Equal = 0x2E;
        inline constexpr ControlCode LeftBracket = 0x2F;
        inline constexpr ControlCode RightBracket = 0x30;
        inline constexpr ControlCode Backslash = 0x31;
        inline constexpr ControlCode Semicolon = 0x33;
        inline constexpr ControlCode Apostrophe = 0x34;
        inline constexpr ControlCode Grave = 0x35;
        inline constexpr ControlCode Comma = 0x36;
        inline constexpr ControlCode Period = 0x37;
        inline constexpr ControlCode Slash = 0x38;
        inline constexpr ControlCode CapsLock = 0x39;

        inline constexpr ControlCode F1 = 0x3A;
        inline constexpr ControlCode F2 = 0x3B;
        inline constexpr ControlCode F3 = 0x3C;
        inline constexpr ControlCode F4 = 0x3D;
        inline constexpr ControlCode F5 = 0x3E;
        inline constexpr ControlCode F6 = 0x3F;
        inline constexpr ControlCode F7 = 0x40;
        inline constexpr ControlCode F8 = 0x41;
        inline constexpr ControlCode F9 = 0x42;
        inline constexpr ControlCode F10 = 0x43;
        inline constexpr ControlCode F11 = 0x44;
        inline constexpr ControlCode F12 = 0x45;

        inline constexpr ControlCode PrintScreen = 0x46;
        inline constexpr ControlCode ScrollLock = 0x47;
        inline constexpr ControlCode Pause = 0x48;
        inline constexpr ControlCode Insert = 0x49;
        inline constexpr ControlCode Home = 0x4A;
        inline constexpr ControlCode PageUp = 0x4B;
        inline constexpr ControlCode Delete = 0x4C;
        inline constexpr ControlCode End = 0x4D;
        inline constexpr ControlCode PageDown = 0x4E;
        inline constexpr ControlCode RightArrow = 0x4F;
        inline constexpr ControlCode LeftArrow = 0x50;
        inline constexpr ControlCode DownArrow = 0x51;
        inline constexpr ControlCode UpArrow = 0x52;

        inline constexpr ControlCode NumLock = 0x53;
        inline constexpr ControlCode KeypadDivide = 0x54;
        inline constexpr ControlCode KeypadMultiply = 0x55;
        inline constexpr ControlCode KeypadMinus = 0x56;
        inline constexpr ControlCode KeypadPlus = 0x57;
        inline constexpr ControlCode KeypadEnter = 0x58;
        inline constexpr ControlCode Keypad1 = 0x59;
        inline constexpr ControlCode Keypad2 = 0x5A;
        inline constexpr ControlCode Keypad3 = 0x5B;
        inline constexpr ControlCode Keypad4 = 0x5C;
        inline constexpr ControlCode Keypad5 = 0x5D;
        inline constexpr ControlCode Keypad6 = 0x5E;
        inline constexpr ControlCode Keypad7 = 0x5F;
        inline constexpr ControlCode Keypad8 = 0x60;
        inline constexpr ControlCode Keypad9 = 0x61;
        inline constexpr ControlCode Keypad0 = 0x62;
        inline constexpr ControlCode KeypadDecimal = 0x63;
        inline constexpr ControlCode NonUsBackslash = 0x64;
        inline constexpr ControlCode Application = 0x65;

        inline constexpr ControlCode LeftControl = 0xE0;
        inline constexpr ControlCode LeftShift = 0xE1;
        inline constexpr ControlCode LeftAlt = 0xE2;
        inline constexpr ControlCode LeftSuper = 0xE3;
        inline constexpr ControlCode RightControl = 0xE4;
        inline constexpr ControlCode RightShift = 0xE5;
        inline constexpr ControlCode RightAlt = 0xE6;
        inline constexpr ControlCode RightSuper = 0xE7;
    }

    /// @brief Compares controls for equality.
    /// @param left First control to compare.
    /// @param right Second control to compare.
    /// @return True if both controls identify the same physical input.
    constexpr bool operator==(const InputControl &left, const InputControl &right)
    {
        return left.deviceType == right.deviceType &&
               left.deviceIndex == right.deviceIndex &&
               left.controlType == right.controlType &&
               left.controlCode == right.controlCode;
    }

    /// @brief Orders controls for use as map keys.
    /// @param left First control to compare.
    /// @param right Second control to compare.
    /// @return True if left sorts before right.
    constexpr bool operator<(const InputControl &left, const InputControl &right)
    {
        if (left.deviceType != right.deviceType)
        {
            return static_cast<int>(left.deviceType) < static_cast<int>(right.deviceType);
        }

        if (left.deviceIndex != right.deviceIndex)
        {
            return left.deviceIndex < right.deviceIndex;
        }

        if (left.controlType != right.controlType)
        {
            return static_cast<int>(left.controlType) < static_cast<int>(right.controlType);
        }

        return left.controlCode < right.controlCode;
    }

    /// @brief Creates a keyboard button control.
    /// @param controlCode USB HID keyboard usage ID.
    /// @return Keyboard button control.
    constexpr InputControl makeKeyboardKey(ControlCode controlCode)
    {
        return InputControl{InputDeviceType::Keyboard, 0, InputControlType::Button, controlCode};
    }

    /// @brief Creates a mouse button control.
    /// @param button Mouse button to identify.
    /// @return Mouse button control.
    constexpr InputControl makeMouseButton(MouseButton button)
    {
        return InputControl{InputDeviceType::Mouse, 0, InputControlType::Button, static_cast<ControlCode>(button)};
    }

    /// @brief Creates a mouse wheel control.
    /// @param wheel Mouse wheel axis to identify.
    /// @return Mouse wheel control.
    constexpr InputControl makeMouseWheel(MouseWheel wheel)
    {
        return InputControl{InputDeviceType::Mouse, 0, InputControlType::Wheel, static_cast<ControlCode>(wheel)};
    }

    /// @brief Creates a mouse movement axis control.
    /// @param axis Mouse movement axis to identify.
    /// @return Mouse movement axis control.
    constexpr InputControl makeMouseAxis(MouseAxis axis)
    {
        return InputControl{InputDeviceType::Mouse, 0, InputControlType::Axis, static_cast<ControlCode>(axis)};
    }

    /// @brief Creates a gamepad button control.
    /// @param deviceIndex Gamepad slot.
    /// @param button Gamepad button to identify.
    /// @return Gamepad button control.
    constexpr InputControl makeGamepadButton(DeviceIndex deviceIndex, GamepadButton button)
    {
        return InputControl{InputDeviceType::Gamepad, deviceIndex, InputControlType::Button, static_cast<ControlCode>(button)};
    }

    /// @brief Creates a gamepad axis control.
    /// @param deviceIndex Gamepad slot.
    /// @param axis Gamepad axis to identify.
    /// @return Gamepad axis control.
    constexpr InputControl makeGamepadAxis(DeviceIndex deviceIndex, GamepadAxis axis)
    {
        return InputControl{InputDeviceType::Gamepad, deviceIndex, InputControlType::Axis, static_cast<ControlCode>(axis)};
    }

    namespace Internal
    {
        struct InputStateAccess; // Grants platform backends controlled write access to InputState.
    }

    /// @brief Platform-independent physical input state.
    class InputState
    {
    public:
        // State lifecycle

        /// @brief Creates input state with small reserved storage for common controls.
        InputState();

        /// @brief Clears active input state while preserving device connections.
        /// @param emitReleaseActivations True to emit releases for currently held buttons.
        void clear(bool emitReleaseActivations = true);

        /// @brief Advances to a new frame.
        void advanceFrame();

        // Buttons

        /// @brief Checks whether a button is held.
        /// @param control Button to query.
        /// @return True if held.
        bool isButtonDown(InputControl control) const;

        /// @brief Checks whether a button was pressed this frame.
        /// @param control Button to query.
        /// @return True if pressed.
        bool wasButtonPressed(InputControl control) const;

        /// @brief Checks whether a button was released this frame.
        /// @param control Button to query.
        /// @return True if released.
        bool wasButtonReleased(InputControl control) const;

        /// @brief Returns currently held buttons without copying.
        /// @return Read-only view of held button controls.
        std::span<const InputControl> getCurrentButtonView() const;

        // Axes and pointer state

        /// @brief Returns an axis value.
        /// @param control Axis to query.
        /// @return Current axis value, or 0 if unset.
        float getAxis(InputControl control) const;

        /// @brief Returns raw mouse X movement.
        /// @return Relative x movement accumulated this frame.
        int getMouseDeltaX() const;

        /// @brief Returns raw mouse Y movement.
        /// @return Relative y movement accumulated this frame.
        int getMouseDeltaY() const;

        /// @brief Returns whether mouse position is known.
        /// @return True if the latest mouse position is valid.
        bool hasMousePosition() const;

        /// @brief Returns the latest mouse X position.
        /// @return Latest client-area x position, or 0 if position is unknown.
        int getMouseX() const;

        /// @brief Returns the latest mouse Y position.
        /// @return Latest client-area y position, or 0 if position is unknown.
        int getMouseY() const;

        /// @brief Returns wheel movement.
        /// @param control Wheel to query.
        /// @return Wheel amount, or 0 if unset.
        float getWheelDelta(InputControl control) const;

        // Devices and activations

        /// @brief Returns whether a device is connected.
        /// @note Keyboard and mouse slot 0 are treated as always connected; gamepads use backend-tracked connection state.
        /// @param deviceType Device family.
        /// @param deviceIndex Device slot.
        /// @return True if connected.
        bool isDeviceConnected(InputDeviceType deviceType, DeviceIndex deviceIndex) const;

        /// @brief Returns the first activation.
        /// @param outActivation First activation if one exists.
        /// @return True if an activation was found.
        bool tryGetFirstActivation(InputActivation &outActivation) const;

        /// @brief Returns all activations.
        /// @param outActivations All activations.
        void getActivations(std::vector<InputActivation> &outActivations) const;

        /// @brief Returns current-frame activations without copying.
        /// @return Read-only view of activations in detection order.
        std::span<const InputActivation> getActivationView() const;

        // Text input

        /// @brief Returns whether text input was received.
        /// @return True if typed text was received this frame.
        bool hasTextInput() const;

        /// @brief Returns typed text input.
        /// @return UTF-8 text received this frame.
        std::string_view getTextInputUtf8() const;

    private:
        // Internal bridge

        /// @brief Grants platform backends access to private mutation helpers.
        friend struct Internal::InputStateAccess;

        // Stored state

        /// @brief Physical input device identifier used by connection tracking.
        struct InputDeviceId
        {
            InputDeviceType deviceType = InputDeviceType::Keyboard; // Device family.
            DeviceIndex deviceIndex = 0;                            // Device slot.
        };

        /// @brief Currently held button controls.
        std::vector<InputControl> currentButtons{};

        /// @brief Button presses detected this frame.
        std::vector<InputControl> pressedButtons{};

        /// @brief Button releases detected this frame.
        std::vector<InputControl> releasedButtons{};

        /// @brief Current non-zero axis values.
        std::vector<std::pair<InputControl, float>> axisValues{};

        /// @brief Current-frame non-zero wheel movement.
        std::vector<std::pair<InputControl, float>> wheelDeltas{};

        /// @brief Explicitly connected backend-tracked devices.
        std::vector<InputDeviceId> connectedDevices{};

        /// @brief Bindable changes detected this frame.
        std::vector<InputActivation> activations{};

        /// @brief Typed text accumulated this frame.
        std::string textInputUtf8{};

        /// @brief Increments whenever persistent input state is cleared.
        std::uint64_t clearGeneration = 0;

        /// @brief Pending UTF-16 high surrogate for text input.
        char16_t pendingTextHighSurrogate = 0;

        /// @brief Current-frame raw mouse X movement.
        int mouseDeltaX = 0;

        /// @brief Current-frame raw mouse Y movement.
        int mouseDeltaY = 0;

        /// @brief Latest known cursor X position.
        int mouseX = 0;

        /// @brief Latest known cursor Y position.
        int mouseY = 0;

        /// @brief True while absolute mouse position is valid.
        bool mousePositionKnown = false;

        // Mutation helpers

        /// @brief Sets a button's held state.
        /// @param control Button control to update.
        /// @param isDown True when the button is down.
        void setButtonInternal(InputControl control, bool isDown);

        /// @brief Sets an axis value.
        /// @param control Axis control to update.
        /// @param value New axis value.
        void setAxisInternal(InputControl control, float value);

        /// @brief Adds raw mouse movement.
        /// @param deltaX Relative x movement.
        /// @param deltaY Relative y movement.
        void addMouseDeltaInternal(int deltaX, int deltaY);

        /// @brief Sets the mouse position.
        /// @param x Client-area x position.
        /// @param y Client-area y position.
        void setMousePositionInternal(int x, int y);

        /// @brief Clears the mouse position.
        void clearMousePositionInternal();

        /// @brief Adds wheel movement.
        /// @param control Wheel control to update.
        /// @param amount Wheel movement amount.
        void addWheelDeltaInternal(InputControl control, float amount);

        /// @brief Adds typed text.
        /// @param text UTF-8 text to append.
        void addTextUtf8Internal(std::string_view text);

        /// @brief Adds a typed codepoint.
        /// @param codepoint Unicode codepoint to append.
        void addTextCodepointInternal(char32_t codepoint);

        /// @brief Returns the pending UTF-16 high surrogate for text input.
        /// @return Pending high surrogate, or 0 if none exists.
        char16_t getPendingTextHighSurrogateInternal() const;

        /// @brief Stores the pending UTF-16 high surrogate for text input.
        /// @param codeUnit UTF-16 high surrogate to store.
        void setPendingTextHighSurrogateInternal(char16_t codeUnit);

        /// @brief Clears pending text composition state.
        void clearTextCompositionInternal();

        /// @brief Sets device connection state.
        /// @param deviceType Device family.
        /// @param deviceIndex Device slot.
        /// @param connected True when the device is connected.
        void setDeviceConnectedInternal(InputDeviceType deviceType, DeviceIndex deviceIndex, bool connected);
    };
}
