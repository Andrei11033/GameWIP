/// @file win32_terminal_events.cpp
/// @brief Allocation-free Win32 KEY_EVENT_RECORD normalization for Terminal.

#include "terminal/platform/win32/win32_terminal_events.h"

#include "unicode/unicode.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <variant>

namespace GameWIP::Terminal::Detail::Platform::Win32Events
{
    namespace
    {
        using CharacterKey = Terminal::Types::CharacterKey;
        using FunctionKey = Terminal::Types::FunctionKey;
        using Key = Terminal::Types::Key;
        using KeyAction = Terminal::Types::KeyAction;
        using KeyEvent = Terminal::Types::KeyEvent;
        using KeyLocation = Terminal::Types::KeyLocation;
        using KeyModifier = Terminal::Types::KeyModifier;
        using MediaKey = Terminal::Types::MediaKey;
        using ModifierKey = Terminal::Types::ModifierKey;
        using NamedKey = Terminal::Types::NamedKey;

        inline constexpr std::size_t kTrackedLocationCount = 5;

        [[nodiscard]] KeyLocation keyLocation(const KEY_EVENT_RECORD &record) noexcept;

        [[nodiscard]] constexpr std::size_t trackingIndex(WORD virtualKey, KeyLocation location) noexcept
        {
            return static_cast<std::size_t>(virtualKey) * kTrackedLocationCount + static_cast<std::size_t>(location);
        }

        [[nodiscard]] constexpr KeyModifier clearModifier(KeyModifier value, KeyModifier bit) noexcept
        {
            return static_cast<KeyModifier>(static_cast<std::uint16_t>(value) & ~static_cast<std::uint16_t>(bit));
        }

        [[nodiscard]] KeyModifier modifiersFromControlState(DWORD state, const DecoderState &decoderState) noexcept
        {
            KeyModifier modifiers = KeyModifier::None;
            if ((state & SHIFT_PRESSED) != 0)
            {
                modifiers |= KeyModifier::Shift;
            }
            if ((state & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) != 0)
            {
                modifiers |= KeyModifier::Control;
            }
            if ((state & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED)) != 0)
            {
                modifiers |= KeyModifier::Alt;
            }
            if ((state & CAPSLOCK_ON) != 0)
            {
                modifiers |= KeyModifier::CapsLock;
            }
            if ((state & NUMLOCK_ON) != 0)
            {
                modifiers |= KeyModifier::NumLock;
            }
            if ((state & SCROLLLOCK_ON) != 0)
            {
                modifiers |= KeyModifier::ScrollLock;
            }

            const std::size_t leftSuper = trackingIndex(VK_LWIN, KeyLocation::Left);
            const std::size_t rightSuper = trackingIndex(VK_RWIN, KeyLocation::Right);
            if ((leftSuper < decoderState.keyDown.size() && decoderState.keyDown.test(leftSuper)) ||
                (rightSuper < decoderState.keyDown.size() && decoderState.keyDown.test(rightSuper)))
            {
                modifiers |= KeyModifier::Super;
            }
            return modifiers;
        }

        [[nodiscard]] bool isPrintable(char32_t scalar) noexcept
        {
            return scalar >= U' ' && scalar != static_cast<char32_t>(0x7f);
        }

        [[nodiscard]] KeyModifier normalizedCharacterModifiers(
            const KEY_EVENT_RECORD &record,
            char32_t scalar,
            const DecoderState &decoderState) noexcept
        {
            KeyModifier modifiers = modifiersFromControlState(record.dwControlKeyState, decoderState);

            // Windows commonly represents AltGr as synthetic Left Ctrl + Right Alt. When that combination
            // produced ordinary Unicode text, expose the text rather than a fake Ctrl+Alt shortcut.
            const bool altGr = (record.dwControlKeyState & RIGHT_ALT_PRESSED) != 0 && (record.dwControlKeyState & LEFT_CTRL_PRESSED) != 0;
            if (altGr && isPrintable(scalar))
            {
                modifiers = clearModifier(modifiers, KeyModifier::Control);
                modifiers = clearModifier(modifiers, KeyModifier::Alt);
            }
            return modifiers;
        }

        [[nodiscard]] std::optional<ModifierKey> modifierKey(WORD virtualKey) noexcept
        {
            switch (virtualKey)
            {
            case VK_SHIFT:
            case VK_LSHIFT:
            case VK_RSHIFT:
                return ModifierKey::Shift;
            case VK_CONTROL:
            case VK_LCONTROL:
            case VK_RCONTROL:
                return ModifierKey::Control;
            case VK_MENU:
            case VK_LMENU:
            case VK_RMENU:
                return ModifierKey::Alt;
            case VK_LWIN:
            case VK_RWIN:
                return ModifierKey::Super;
            default:
                return std::nullopt;
            }
        }

        [[nodiscard]] std::optional<NamedKey> namedKey(WORD virtualKey) noexcept
        {
            switch (virtualKey)
            {
            case VK_BACK:
                return NamedKey::Backspace;
            case VK_TAB:
                return NamedKey::Tab;
            case VK_RETURN:
                return NamedKey::Enter;
            case VK_ESCAPE:
                return NamedKey::Escape;
            case VK_INSERT:
                return NamedKey::Insert;
            case VK_DELETE:
                return NamedKey::Delete;
            case VK_HOME:
                return NamedKey::Home;
            case VK_END:
                return NamedKey::End;
            case VK_PRIOR:
                return NamedKey::PageUp;
            case VK_NEXT:
                return NamedKey::PageDown;
            case VK_UP:
                return NamedKey::ArrowUp;
            case VK_DOWN:
                return NamedKey::ArrowDown;
            case VK_LEFT:
                return NamedKey::ArrowLeft;
            case VK_RIGHT:
                return NamedKey::ArrowRight;
            case VK_CLEAR:
                return NamedKey::Begin;
            case VK_CAPITAL:
                return NamedKey::CapsLock;
            case VK_NUMLOCK:
                return NamedKey::NumLock;
            case VK_SCROLL:
                return NamedKey::ScrollLock;
            case VK_SNAPSHOT:
                return NamedKey::PrintScreen;
            case VK_PAUSE:
            case VK_CANCEL:
                return NamedKey::Pause;
            case VK_APPS:
                return NamedKey::Menu;
            default:
                return std::nullopt;
            }
        }

        [[nodiscard]] std::optional<MediaKey> mediaKey(WORD virtualKey) noexcept
        {
            switch (virtualKey)
            {
            case VK_MEDIA_PLAY_PAUSE:
                return MediaKey::PlayPause;
            case VK_MEDIA_STOP:
                return MediaKey::Stop;
            case VK_MEDIA_NEXT_TRACK:
                return MediaKey::NextTrack;
            case VK_MEDIA_PREV_TRACK:
                return MediaKey::PreviousTrack;
            case VK_VOLUME_UP:
                return MediaKey::VolumeUp;
            case VK_VOLUME_DOWN:
                return MediaKey::VolumeDown;
            case VK_VOLUME_MUTE:
                return MediaKey::Mute;
            default:
                return std::nullopt;
            }
        }

        [[nodiscard]] std::optional<FunctionKey> functionKey(WORD virtualKey) noexcept
        {
            if (virtualKey >= VK_F1 && virtualKey <= VK_F24)
            {
                return FunctionKey{.number = static_cast<std::uint16_t>(virtualKey - VK_F1 + 1)};
            }
            return std::nullopt;
        }

        [[nodiscard]] bool isNavigationVirtualKey(WORD virtualKey) noexcept
        {
            switch (virtualKey)
            {
            case VK_INSERT:
            case VK_DELETE:
            case VK_HOME:
            case VK_END:
            case VK_PRIOR:
            case VK_NEXT:
            case VK_UP:
            case VK_DOWN:
            case VK_LEFT:
            case VK_RIGHT:
            case VK_CLEAR:
                return true;
            default:
                return false;
            }
        }

        [[nodiscard]] bool isNumpadVirtualKey(WORD virtualKey) noexcept
        {
            return (virtualKey >= VK_NUMPAD0 && virtualKey <= VK_NUMPAD9) || virtualKey == VK_MULTIPLY || virtualKey == VK_ADD ||
                   virtualKey == VK_SEPARATOR || virtualKey == VK_SUBTRACT || virtualKey == VK_DECIMAL || virtualKey == VK_DIVIDE;
        }

        [[nodiscard]] KeyLocation keyLocation(const KEY_EVENT_RECORD &record) noexcept
        {
            switch (record.wVirtualKeyCode)
            {
            case VK_LSHIFT:
            case VK_LCONTROL:
            case VK_LMENU:
            case VK_LWIN:
                return KeyLocation::Left;
            case VK_RSHIFT:
            case VK_RCONTROL:
            case VK_RMENU:
            case VK_RWIN:
                return KeyLocation::Right;
            case VK_CONTROL:
            case VK_MENU:
                return (record.dwControlKeyState & ENHANCED_KEY) != 0 ? KeyLocation::Right : KeyLocation::Left;
            case VK_SHIFT:
            {
                const UINT leftScan = MapVirtualKeyW(VK_LSHIFT, MAPVK_VK_TO_VSC);
                const UINT rightScan = MapVirtualKeyW(VK_RSHIFT, MAPVK_VK_TO_VSC);
                if (record.wVirtualScanCode == leftScan)
                {
                    return KeyLocation::Left;
                }
                if (record.wVirtualScanCode == rightScan)
                {
                    return KeyLocation::Right;
                }
                return KeyLocation::Unknown;
            }
            case VK_RETURN:
                return (record.dwControlKeyState & ENHANCED_KEY) != 0 ? KeyLocation::Numpad : KeyLocation::Standard;
            default:
                break;
            }

            if (isNumpadVirtualKey(record.wVirtualKeyCode))
            {
                return KeyLocation::Numpad;
            }
            if (isNavigationVirtualKey(record.wVirtualKeyCode) && (record.dwControlKeyState & ENHANCED_KEY) == 0)
            {
                return KeyLocation::Numpad;
            }
            return KeyLocation::Standard;
        }

        [[nodiscard]] KeyModifier modifiersForKey(const KEY_EVENT_RECORD &record, const Key &key, const DecoderState &decoderState) noexcept
        {
            KeyModifier modifiers = modifiersFromControlState(record.dwControlKeyState, decoderState);
            if (const auto *character = std::get_if<CharacterKey>(&key))
            {
                return normalizedCharacterModifiers(record, character->value, decoderState);
            }

            if (const auto *modifier = std::get_if<ModifierKey>(&key))
            {
                switch (*modifier)
                {
                case ModifierKey::Shift:
                    return clearModifier(modifiers, KeyModifier::Shift);
                case ModifierKey::Control:
                    return clearModifier(modifiers, KeyModifier::Control);
                case ModifierKey::Alt:
                    return clearModifier(modifiers, KeyModifier::Alt);
                case ModifierKey::Super:
                    return clearModifier(modifiers, KeyModifier::Super);
                case ModifierKey::Hyper:
                    return clearModifier(modifiers, KeyModifier::Hyper);
                case ModifierKey::Meta:
                    return clearModifier(modifiers, KeyModifier::Meta);
                }
            }

            return modifiers;
        }

        [[nodiscard]] KeyDecodeResult makeEvent(Key key, const KEY_EVENT_RECORD &record, DecoderState &state) noexcept
        {
            KeyDecodeResult result;
            result.status = IO::successStatus();
            result.disposition = KeyDecodeDisposition::Produced;

            const KeyLocation location = keyLocation(record);
            const std::size_t keyIndex = trackingIndex(record.wVirtualKeyCode, location);
            const bool trackKey = keyIndex < state.keyDown.size() && record.wVirtualKeyCode != 0 && record.wVirtualKeyCode != VK_PACKET;
            const bool wasDown = trackKey && state.keyDown.test(keyIndex);

            KeyAction action = KeyAction::Release;
            std::uint32_t repeatCount = 1;
            const std::uint32_t nativeRepeat = record.wRepeatCount == 0 ? 1U : static_cast<std::uint32_t>(record.wRepeatCount);

            if (record.bKeyDown != FALSE)
            {
                if (trackKey)
                {
                    state.keyDown.set(keyIndex);
                }

                if (wasDown)
                {
                    action = KeyAction::Repeat;
                    repeatCount = nativeRepeat;
                }
                else
                {
                    action = KeyAction::Press;
                    repeatCount = 1;
                }
            }
            else if (trackKey)
            {
                state.keyDown.reset(keyIndex);
            }

            const KeyModifier modifiers = modifiersForKey(record, key, state);

            KeyEvent event{.key = key, .modifiers = modifiers, .action = action, .location = location, .repeatCount = repeatCount};

            if (record.bKeyDown != FALSE && !wasDown && nativeRepeat > 1)
            {
                KeyEvent repeated = event;
                repeated.action = KeyAction::Repeat;
                repeated.repeatCount = nativeRepeat - 1;
                state.pendingEvent = Terminal::Types::Event{.data = repeated};
            }

            result.event = Terminal::Types::Event{.data = event};
            return result;
        }

        [[nodiscard]] std::optional<CharacterKey> ctrlCharacter(const KEY_EVENT_RECORD &record) noexcept
        {
            const bool control = (record.dwControlKeyState & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) != 0;
            if (!control)
            {
                return std::nullopt;
            }

            if (record.wVirtualKeyCode >= 'A' && record.wVirtualKeyCode <= 'Z')
            {
                return CharacterKey{.value = static_cast<char32_t>(U'a' + (record.wVirtualKeyCode - 'A'))};
            }
            if (record.wVirtualKeyCode == VK_SPACE)
            {
                return CharacterKey{.value = U' '};
            }
            return std::nullopt;
        }

        [[nodiscard]] KeyDecodeResult decodeCharacterRecord(const KEY_EVENT_RECORD &record, DecoderState &state) noexcept
        {
            const char16_t unit = static_cast<char16_t>(record.uChar.UnicodeChar);

            if (state.pendingHighSurrogate != u'\0')
            {
                if (!Unicode::Utf16::isLowSurrogate(unit))
                {
                    state.pendingHighSurrogate = u'\0';
                    state.pendingHighSurrogateRecord = {};
                    return {
                        .status = IO::makeStatus(IO::Types::ErrorCode::EncodingFailed),
                        .disposition = KeyDecodeDisposition::Failed,
                        .event = std::nullopt};
                }

                const std::array<char16_t, 2> pair{state.pendingHighSurrogate, unit};
                const KEY_EVENT_RECORD firstRecord = state.pendingHighSurrogateRecord;
                state.pendingHighSurrogate = u'\0';
                state.pendingHighSurrogateRecord = {};

                const Unicode::Types::Utf16DecodeResult decoded = Unicode::Utf16::decodeScalar(pair);
                if (decoded.outcome != Unicode::Types::DecodeOutcome::Decoded)
                {
                    return {
                        .status = IO::makeStatus(IO::Types::ErrorCode::EncodingFailed),
                        .disposition = KeyDecodeDisposition::Failed,
                        .event = std::nullopt};
                }

                return makeEvent(CharacterKey{.value = decoded.scalar}, firstRecord, state);
            }

            if (Unicode::Utf16::isHighSurrogate(unit))
            {
                state.pendingHighSurrogate = unit;
                state.pendingHighSurrogateRecord = record;
                return {.status = IO::successStatus(), .disposition = KeyDecodeDisposition::Pending, .event = std::nullopt};
            }

            if (Unicode::Utf16::isLowSurrogate(unit))
            {
                return {
                    .status = IO::makeStatus(IO::Types::ErrorCode::EncodingFailed),
                    .disposition = KeyDecodeDisposition::Failed,
                    .event = std::nullopt};
            }

            if (unit >= static_cast<char16_t>(0x0001) && unit <= static_cast<char16_t>(0x001a))
            {
                if (const std::optional<CharacterKey> character = ctrlCharacter(record))
                {
                    return makeEvent(*character, record, state);
                }
            }

            if (unit != u'\0')
            {
                const std::array<char16_t, 1> single{unit};
                const Unicode::Types::Utf16DecodeResult decoded = Unicode::Utf16::decodeScalar(single);
                if (decoded.outcome != Unicode::Types::DecodeOutcome::Decoded)
                {
                    return {
                        .status = IO::makeStatus(IO::Types::ErrorCode::EncodingFailed),
                        .disposition = KeyDecodeDisposition::Failed,
                        .event = std::nullopt};
                }
                return makeEvent(CharacterKey{.value = decoded.scalar}, record, state);
            }

            if (const std::optional<CharacterKey> character = ctrlCharacter(record))
            {
                return makeEvent(*character, record, state);
            }

            return {.status = IO::successStatus(), .disposition = KeyDecodeDisposition::Ignored, .event = std::nullopt};
        }
    } // namespace

    void DecoderState::clear() noexcept
    {
        keyDown.reset();
        pendingHighSurrogate = u'\0';
        pendingHighSurrogateRecord = {};
        pendingEvent.reset();
    }

    std::optional<Terminal::Types::Event> takePendingEvent(DecoderState &state) noexcept
    {
        std::optional<Terminal::Types::Event> event = std::move(state.pendingEvent);
        state.pendingEvent.reset();
        return event;
    }

    KeyDecodeResult decodeKeyRecord(const KEY_EVENT_RECORD &record, DecoderState &state) noexcept
    {
        // Once a surrogate pair starts, only another key record carrying the low surrogate can finish it.
        if (state.pendingHighSurrogate != u'\0')
        {
            return decodeCharacterRecord(record, state);
        }

        if (const std::optional<ModifierKey> modifier = modifierKey(record.wVirtualKeyCode))
        {
            return makeEvent(*modifier, record, state);
        }

        if (const std::optional<NamedKey> named = namedKey(record.wVirtualKeyCode))
        {
            return makeEvent(*named, record, state);
        }

        if (const std::optional<FunctionKey> function = functionKey(record.wVirtualKeyCode))
        {
            return makeEvent(*function, record, state);
        }

        if (const std::optional<MediaKey> media = mediaKey(record.wVirtualKeyCode))
        {
            return makeEvent(*media, record, state);
        }

        return decodeCharacterRecord(record, state);
    }
} // namespace GameWIP::Terminal::Detail::Platform::Win32Events
