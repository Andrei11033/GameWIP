/// @file win32_terminal_test_hooks.cpp
/// @brief Win32 Terminal validation-hook routing.

#include "terminal/internal/terminal_platform.h"
#include "terminal/internal/terminal_test_hooks.h"
#include "terminal/platform/win32/internal/win32_terminal_test_hooks.h"

#if TERMINAL_INTERNAL_TEST_HOOKS
namespace GameWIP::Terminal::Detail::Platform::TestHooks
{
    /// ------------------------------------------------------------
    /// Input reset
    /// ------------------------------------------------------------

    void resetInputState() noexcept
    {
        resetWin32InputState();
        resetWin32KeyDecoder();
        setPendingHighSurrogate(Terminal::Types::Input::Stream::Stdin, 0);
    }
} // namespace GameWIP::Terminal::Detail::Platform::TestHooks

namespace GameWIP::Terminal::TestHooks
{
    /// ------------------------------------------------------------
    /// Win32 key decoding
    /// ------------------------------------------------------------

    void resetWin32KeyDecoder() noexcept
    {
        Detail::Platform::TestHooks::resetWin32KeyDecoder();
    }

    std::size_t consoleWaitCallCount() noexcept
    {
        return Detail::Platform::TestHooks::consoleWaitCallCount();
    }

    Win32KeyDecodeResult decodeWin32KeyRecord(
        bool keyDown,
        std::uint16_t virtualKey,
        char16_t unicodeCharacter,
        std::uint32_t controlState,
        std::uint16_t repeatCount,
        std::uint16_t scanCode) noexcept
    {
        return Detail::Platform::TestHooks::decodeWin32KeyRecord(keyDown, virtualKey, unicodeCharacter, controlState, repeatCount, scanCode);
    }

    std::optional<Terminal::Types::Event> takePendingWin32KeyEvent() noexcept
    {
        return Detail::Platform::TestHooks::takePendingWin32KeyEvent();
    }

    /// ------------------------------------------------------------
    /// Input endpoint state
    /// ------------------------------------------------------------

    void setPendingHighSurrogate(Terminal::Types::Input::Stream stream, std::uint16_t surrogate) noexcept
    {
        Detail::Platform::TestHooks::setPendingHighSurrogate(stream, surrogate);
    }

    bool hasPendingHighSurrogate(Terminal::Types::Input::Stream stream) noexcept
    {
        return Detail::Platform::TestHooks::hasPendingHighSurrogate(stream);
    }
} // namespace GameWIP::Terminal::TestHooks
#endif
