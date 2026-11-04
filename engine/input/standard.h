/// @file standard.h
/// @brief Predefined identifiers for common input controls.
#pragma once

#include "input/types.h"

/// @brief Backend-neutral standard-control vocabulary.
/// @details These identifiers describe controls before a device is selected.
/// They are not device classes, backend selectors, or character-layout results.
/// Values that are derived from HID use the corresponding HID usage value.
/// Controls outside this vocabulary remain available through Types::HidControlId.
/// The named keyboard vocabulary is intentionally limited to the ANSI 104-key
/// full-size baseline. Controls outside this vocabulary remain available by
/// their HID identity when a device exposes them.
namespace GameWIP::Input::Standard
{
    // The short names are intentional vocabulary: Standard::Key::W and
    // Standard::Gamepad::South are the stable identifiers users bind to.
    // NOLINTBEGIN(readability-identifier-naming)

    /// @brief ANSI 104-key full-size keyboard usage identifiers.
    /// @details This is the stable programmer-facing baseline for preassigned
    /// controls. Regional, extended, vendor, and optional HID keyboard usages
    /// remain discoverable through Types::HidControlId.
    /// @note Values identify physical controls, not characters produced by a keyboard layout.
    namespace Key
    {
        inline constexpr Types::StandardId A{Types::StandardDomain::Keyboard, 0x04}; ///< Keyboard A.
        inline constexpr Types::StandardId B{Types::StandardDomain::Keyboard, 0x05}; ///< Keyboard B.
        inline constexpr Types::StandardId C{Types::StandardDomain::Keyboard, 0x06}; ///< Keyboard C.
        inline constexpr Types::StandardId D{Types::StandardDomain::Keyboard, 0x07}; ///< Keyboard D.
        inline constexpr Types::StandardId E{Types::StandardDomain::Keyboard, 0x08}; ///< Keyboard E.
        inline constexpr Types::StandardId F{Types::StandardDomain::Keyboard, 0x09}; ///< Keyboard F.
        inline constexpr Types::StandardId G{Types::StandardDomain::Keyboard, 0x0A}; ///< Keyboard G.
        inline constexpr Types::StandardId H{Types::StandardDomain::Keyboard, 0x0B}; ///< Keyboard H.
        inline constexpr Types::StandardId I{Types::StandardDomain::Keyboard, 0x0C}; ///< Keyboard I.
        inline constexpr Types::StandardId J{Types::StandardDomain::Keyboard, 0x0D}; ///< Keyboard J.
        inline constexpr Types::StandardId K{Types::StandardDomain::Keyboard, 0x0E}; ///< Keyboard K.
        inline constexpr Types::StandardId L{Types::StandardDomain::Keyboard, 0x0F}; ///< Keyboard L.
        inline constexpr Types::StandardId M{Types::StandardDomain::Keyboard, 0x10}; ///< Keyboard M.
        inline constexpr Types::StandardId N{Types::StandardDomain::Keyboard, 0x11}; ///< Keyboard N.
        inline constexpr Types::StandardId O{Types::StandardDomain::Keyboard, 0x12}; ///< Keyboard O.
        inline constexpr Types::StandardId P{Types::StandardDomain::Keyboard, 0x13}; ///< Keyboard P.
        inline constexpr Types::StandardId Q{Types::StandardDomain::Keyboard, 0x14}; ///< Keyboard Q.
        inline constexpr Types::StandardId R{Types::StandardDomain::Keyboard, 0x15}; ///< Keyboard R.
        inline constexpr Types::StandardId S{Types::StandardDomain::Keyboard, 0x16}; ///< Keyboard S.
        inline constexpr Types::StandardId T{Types::StandardDomain::Keyboard, 0x17}; ///< Keyboard T.
        inline constexpr Types::StandardId U{Types::StandardDomain::Keyboard, 0x18}; ///< Keyboard U.
        inline constexpr Types::StandardId V{Types::StandardDomain::Keyboard, 0x19}; ///< Keyboard V.
        inline constexpr Types::StandardId W{Types::StandardDomain::Keyboard, 0x1A}; ///< Keyboard W.
        inline constexpr Types::StandardId X{Types::StandardDomain::Keyboard, 0x1B}; ///< Keyboard X.
        inline constexpr Types::StandardId Y{Types::StandardDomain::Keyboard, 0x1C}; ///< Keyboard Y.
        inline constexpr Types::StandardId Z{Types::StandardDomain::Keyboard, 0x1D}; ///< Keyboard Z.

        inline constexpr Types::StandardId Digit1{Types::StandardDomain::Keyboard, 0x1E}; ///< Keyboard 1.
        inline constexpr Types::StandardId Digit2{Types::StandardDomain::Keyboard, 0x1F}; ///< Keyboard 2.
        inline constexpr Types::StandardId Digit3{Types::StandardDomain::Keyboard, 0x20}; ///< Keyboard 3.
        inline constexpr Types::StandardId Digit4{Types::StandardDomain::Keyboard, 0x21}; ///< Keyboard 4.
        inline constexpr Types::StandardId Digit5{Types::StandardDomain::Keyboard, 0x22}; ///< Keyboard 5.
        inline constexpr Types::StandardId Digit6{Types::StandardDomain::Keyboard, 0x23}; ///< Keyboard 6.
        inline constexpr Types::StandardId Digit7{Types::StandardDomain::Keyboard, 0x24}; ///< Keyboard 7.
        inline constexpr Types::StandardId Digit8{Types::StandardDomain::Keyboard, 0x25}; ///< Keyboard 8.
        inline constexpr Types::StandardId Digit9{Types::StandardDomain::Keyboard, 0x26}; ///< Keyboard 9.
        inline constexpr Types::StandardId Digit0{Types::StandardDomain::Keyboard, 0x27}; ///< Keyboard 0.

        inline constexpr Types::StandardId Enter{Types::StandardDomain::Keyboard, 0x28};        ///< Enter.
        inline constexpr Types::StandardId Escape{Types::StandardDomain::Keyboard, 0x29};       ///< Escape.
        inline constexpr Types::StandardId Backspace{Types::StandardDomain::Keyboard, 0x2A};    ///< Backspace.
        inline constexpr Types::StandardId Tab{Types::StandardDomain::Keyboard, 0x2B};          ///< Tab.
        inline constexpr Types::StandardId Space{Types::StandardDomain::Keyboard, 0x2C};        ///< Space.
        inline constexpr Types::StandardId Minus{Types::StandardDomain::Keyboard, 0x2D};        ///< Minus.
        inline constexpr Types::StandardId Equals{Types::StandardDomain::Keyboard, 0x2E};       ///< Equals.
        inline constexpr Types::StandardId LeftBracket{Types::StandardDomain::Keyboard, 0x2F};  ///< Left bracket.
        inline constexpr Types::StandardId RightBracket{Types::StandardDomain::Keyboard, 0x30}; ///< Right bracket.
        inline constexpr Types::StandardId Backslash{Types::StandardDomain::Keyboard, 0x31};    ///< Backslash.
        inline constexpr Types::StandardId Semicolon{Types::StandardDomain::Keyboard, 0x33};    ///< Semicolon.
        inline constexpr Types::StandardId Apostrophe{Types::StandardDomain::Keyboard, 0x34};   ///< Apostrophe.
        inline constexpr Types::StandardId Grave{Types::StandardDomain::Keyboard, 0x35};        ///< Grave accent.
        inline constexpr Types::StandardId Comma{Types::StandardDomain::Keyboard, 0x36};        ///< Comma.
        inline constexpr Types::StandardId Period{Types::StandardDomain::Keyboard, 0x37};       ///< Period.
        inline constexpr Types::StandardId Slash{Types::StandardDomain::Keyboard, 0x38};        ///< Slash.
        inline constexpr Types::StandardId CapsLock{Types::StandardDomain::Keyboard, 0x39};     ///< Caps Lock.

        inline constexpr Types::StandardId F1{Types::StandardDomain::Keyboard, 0x3A};  ///< Function key F1.
        inline constexpr Types::StandardId F2{Types::StandardDomain::Keyboard, 0x3B};  ///< Function key F2.
        inline constexpr Types::StandardId F3{Types::StandardDomain::Keyboard, 0x3C};  ///< Function key F3.
        inline constexpr Types::StandardId F4{Types::StandardDomain::Keyboard, 0x3D};  ///< Function key F4.
        inline constexpr Types::StandardId F5{Types::StandardDomain::Keyboard, 0x3E};  ///< Function key F5.
        inline constexpr Types::StandardId F6{Types::StandardDomain::Keyboard, 0x3F};  ///< Function key F6.
        inline constexpr Types::StandardId F7{Types::StandardDomain::Keyboard, 0x40};  ///< Function key F7.
        inline constexpr Types::StandardId F8{Types::StandardDomain::Keyboard, 0x41};  ///< Function key F8.
        inline constexpr Types::StandardId F9{Types::StandardDomain::Keyboard, 0x42};  ///< Function key F9.
        inline constexpr Types::StandardId F10{Types::StandardDomain::Keyboard, 0x43}; ///< Function key F10.
        inline constexpr Types::StandardId F11{Types::StandardDomain::Keyboard, 0x44}; ///< Function key F11.
        inline constexpr Types::StandardId F12{Types::StandardDomain::Keyboard, 0x45}; ///< Function key F12.

        inline constexpr Types::StandardId PrintScreen{Types::StandardDomain::Keyboard, 0x46}; ///< Print Screen.
        inline constexpr Types::StandardId ScrollLock{Types::StandardDomain::Keyboard, 0x47};  ///< Scroll Lock.
        inline constexpr Types::StandardId Pause{Types::StandardDomain::Keyboard, 0x48};       ///< Pause.
        inline constexpr Types::StandardId Insert{Types::StandardDomain::Keyboard, 0x49};      ///< Insert.
        inline constexpr Types::StandardId Home{Types::StandardDomain::Keyboard, 0x4A};        ///< Home.
        inline constexpr Types::StandardId PageUp{Types::StandardDomain::Keyboard, 0x4B};      ///< Page Up.
        inline constexpr Types::StandardId Delete{Types::StandardDomain::Keyboard, 0x4C};      ///< Delete.
        inline constexpr Types::StandardId End{Types::StandardDomain::Keyboard, 0x4D};         ///< End.
        inline constexpr Types::StandardId PageDown{Types::StandardDomain::Keyboard, 0x4E};    ///< Page Down.
        inline constexpr Types::StandardId RightArrow{Types::StandardDomain::Keyboard, 0x4F};  ///< Right Arrow.
        inline constexpr Types::StandardId LeftArrow{Types::StandardDomain::Keyboard, 0x50};   ///< Left Arrow.
        inline constexpr Types::StandardId DownArrow{Types::StandardDomain::Keyboard, 0x51};   ///< Down Arrow.
        inline constexpr Types::StandardId UpArrow{Types::StandardDomain::Keyboard, 0x52};     ///< Up Arrow.

        inline constexpr Types::StandardId NumLock{Types::StandardDomain::Keyboard, 0x53};        ///< Num Lock.
        inline constexpr Types::StandardId KeypadSlash{Types::StandardDomain::Keyboard, 0x54};    ///< Keypad slash.
        inline constexpr Types::StandardId KeypadAsterisk{Types::StandardDomain::Keyboard, 0x55}; ///< Keypad asterisk.
        inline constexpr Types::StandardId KeypadMinus{Types::StandardDomain::Keyboard, 0x56};    ///< Keypad minus.
        inline constexpr Types::StandardId KeypadPlus{Types::StandardDomain::Keyboard, 0x57};     ///< Keypad plus.
        inline constexpr Types::StandardId KeypadEnter{Types::StandardDomain::Keyboard, 0x58};    ///< Keypad Enter.
        inline constexpr Types::StandardId Keypad1{Types::StandardDomain::Keyboard, 0x59};        ///< Keypad 1.
        inline constexpr Types::StandardId Keypad2{Types::StandardDomain::Keyboard, 0x5A};        ///< Keypad 2.
        inline constexpr Types::StandardId Keypad3{Types::StandardDomain::Keyboard, 0x5B};        ///< Keypad 3.
        inline constexpr Types::StandardId Keypad4{Types::StandardDomain::Keyboard, 0x5C};        ///< Keypad 4.
        inline constexpr Types::StandardId Keypad5{Types::StandardDomain::Keyboard, 0x5D};        ///< Keypad 5.
        inline constexpr Types::StandardId Keypad6{Types::StandardDomain::Keyboard, 0x5E};        ///< Keypad 6.
        inline constexpr Types::StandardId Keypad7{Types::StandardDomain::Keyboard, 0x5F};        ///< Keypad 7.
        inline constexpr Types::StandardId Keypad8{Types::StandardDomain::Keyboard, 0x60};        ///< Keypad 8.
        inline constexpr Types::StandardId Keypad9{Types::StandardDomain::Keyboard, 0x61};        ///< Keypad 9.
        inline constexpr Types::StandardId Keypad0{Types::StandardDomain::Keyboard, 0x62};        ///< Keypad 0.
        inline constexpr Types::StandardId KeypadPeriod{Types::StandardDomain::Keyboard, 0x63};   ///< Keypad period.
        inline constexpr Types::StandardId Application{Types::StandardDomain::Keyboard, 0x65};    ///< Application/Menu key.
        inline constexpr Types::StandardId LeftControl{Types::StandardDomain::Keyboard, 0xE0};    ///< Left Control.
        inline constexpr Types::StandardId LeftShift{Types::StandardDomain::Keyboard, 0xE1};      ///< Left Shift.
        inline constexpr Types::StandardId LeftAlt{Types::StandardDomain::Keyboard, 0xE2};        ///< Left Alt.
        inline constexpr Types::StandardId LeftSuper{Types::StandardDomain::Keyboard, 0xE3};      ///< Left Super.
        inline constexpr Types::StandardId RightControl{Types::StandardDomain::Keyboard, 0xE4};   ///< Right Control.
        inline constexpr Types::StandardId RightShift{Types::StandardDomain::Keyboard, 0xE5};     ///< Right Shift.
        inline constexpr Types::StandardId RightAlt{Types::StandardDomain::Keyboard, 0xE6};       ///< Right Alt.
        inline constexpr Types::StandardId RightSuper{Types::StandardDomain::Keyboard, 0xE7};     ///< Right Super.
    } // namespace Key

    /// @brief GameWIP standard vocabulary for pointing-device controls.
    namespace Mouse
    {
        inline constexpr Types::StandardId Left{Types::StandardDomain::Mouse, 0x01};     ///< Primary button.
        inline constexpr Types::StandardId Right{Types::StandardDomain::Mouse, 0x02};    ///< Secondary button.
        inline constexpr Types::StandardId Middle{Types::StandardDomain::Mouse, 0x03};   ///< Middle button.
        inline constexpr Types::StandardId X1{Types::StandardDomain::Mouse, 0x04};       ///< First auxiliary button.
        inline constexpr Types::StandardId X2{Types::StandardDomain::Mouse, 0x05};       ///< Second auxiliary button.
        inline constexpr Types::StandardId Movement{Types::StandardDomain::Mouse, 0x06}; ///< Relative movement vector.
        inline constexpr Types::StandardId Wheel{Types::StandardDomain::Mouse, 0x07};    ///< Wheel delta vector.
        inline constexpr Types::StandardId Button6{Types::StandardDomain::Mouse, 0x08};  ///< Sixth button.
        inline constexpr Types::StandardId Button7{Types::StandardDomain::Mouse, 0x09};  ///< Seventh button.
        inline constexpr Types::StandardId Button8{Types::StandardDomain::Mouse, 0x0A};  ///< Eighth button.
        inline constexpr Types::StandardId Button9{Types::StandardDomain::Mouse, 0x0B};  ///< Ninth button.
        inline constexpr Types::StandardId Button10{Types::StandardDomain::Mouse, 0x0C}; ///< Tenth button.
        inline constexpr Types::StandardId Button11{Types::StandardDomain::Mouse, 0x0D}; ///< Eleventh button.
        inline constexpr Types::StandardId Button12{Types::StandardDomain::Mouse, 0x0E}; ///< Twelfth button.
        inline constexpr Types::StandardId Button13{Types::StandardDomain::Mouse, 0x0F}; ///< Thirteenth button.
        inline constexpr Types::StandardId Button14{Types::StandardDomain::Mouse, 0x10}; ///< Fourteenth button.
        inline constexpr Types::StandardId Button15{Types::StandardDomain::Mouse, 0x11}; ///< Fifteenth button.
        inline constexpr Types::StandardId Button16{Types::StandardDomain::Mouse, 0x12}; ///< Sixteenth button.
    } // namespace Mouse

    /// @brief GameWIP standard vocabulary for conventional gamepad controls.
    namespace Gamepad
    {
        inline constexpr Types::StandardId South{Types::StandardDomain::Gamepad, 0x01};            ///< South face button.
        inline constexpr Types::StandardId East{Types::StandardDomain::Gamepad, 0x02};             ///< East face button.
        inline constexpr Types::StandardId West{Types::StandardDomain::Gamepad, 0x03};             ///< West face button.
        inline constexpr Types::StandardId North{Types::StandardDomain::Gamepad, 0x04};            ///< North face button.
        inline constexpr Types::StandardId DpadUp{Types::StandardDomain::Gamepad, 0x05};           ///< D-pad up.
        inline constexpr Types::StandardId DpadDown{Types::StandardDomain::Gamepad, 0x06};         ///< D-pad down.
        inline constexpr Types::StandardId DpadLeft{Types::StandardDomain::Gamepad, 0x07};         ///< D-pad left.
        inline constexpr Types::StandardId DpadRight{Types::StandardDomain::Gamepad, 0x08};        ///< D-pad right.
        inline constexpr Types::StandardId LeftShoulder{Types::StandardDomain::Gamepad, 0x09};     ///< Left shoulder button.
        inline constexpr Types::StandardId RightShoulder{Types::StandardDomain::Gamepad, 0x0A};    ///< Right shoulder button.
        inline constexpr Types::StandardId Back{Types::StandardDomain::Gamepad, 0x0B};             ///< Back or select button.
        inline constexpr Types::StandardId Start{Types::StandardDomain::Gamepad, 0x0C};            ///< Start or menu button.
        inline constexpr Types::StandardId Guide{Types::StandardDomain::Gamepad, 0x0D};            ///< Guide or home button.
        inline constexpr Types::StandardId LeftStickButton{Types::StandardDomain::Gamepad, 0x0E};  ///< Left-stick click.
        inline constexpr Types::StandardId RightStickButton{Types::StandardDomain::Gamepad, 0x0F}; ///< Right-stick click.
        inline constexpr Types::StandardId LeftStick{Types::StandardDomain::Gamepad, 0x10};        ///< Left-stick vector.
        inline constexpr Types::StandardId RightStick{Types::StandardDomain::Gamepad, 0x11};       ///< Right-stick vector.
        inline constexpr Types::StandardId LeftTrigger{Types::StandardDomain::Gamepad, 0x12};      ///< Left trigger.
        inline constexpr Types::StandardId RightTrigger{Types::StandardDomain::Gamepad, 0x13};     ///< Right trigger.
        inline constexpr Types::StandardId LeftPaddle{Types::StandardDomain::Gamepad, 0x14};       ///< Left rear paddle.
        inline constexpr Types::StandardId RightPaddle{Types::StandardDomain::Gamepad, 0x15};      ///< Right rear paddle.
        inline constexpr Types::StandardId Touchpad{Types::StandardDomain::Gamepad, 0x16};         ///< Touchpad button or surface.
        inline constexpr Types::StandardId Miscellaneous{Types::StandardDomain::Gamepad, 0x17};    ///< Miscellaneous gamepad button.
        inline constexpr Types::StandardId Capture{Types::StandardDomain::Gamepad, 0x18};          ///< Capture or share button.
        inline constexpr Types::StandardId Mute{Types::StandardDomain::Gamepad, 0x19};             ///< Headset mute button.
    } // namespace Gamepad

    /// @brief Window-level pointer controls that are not attributable to a physical mouse.
    namespace Pointer
    {
        inline constexpr Types::StandardId Position{Types::StandardDomain::Pointer, 0x01}; ///< Client-relative pointer position in pixels.
    } // namespace Pointer

    // NOLINTEND(readability-identifier-naming)
} // namespace GameWIP::Input::Standard
