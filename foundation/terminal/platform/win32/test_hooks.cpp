/// @file test_hooks.cpp
/// @brief Win32 Terminal validation-hook routing.

#include "terminal/internal/test_hooks.h"
#include "terminal/platform/win32/internal/test_hooks.h"

#if TERMINAL_INTERNAL_TEST_HOOKS
namespace GameWIP::Terminal::Internal::TestHooks
{
    /// ------------------------------------------------------------
    /// Input reset
    /// ------------------------------------------------------------

    void resetBackendInputState() noexcept
    {
        Terminal::Platform::Win32::Internal::resetWin32InputState();
        Terminal::Platform::Win32::Internal::resetWin32KeyDecoder();
        Terminal::Platform::Win32::Internal::setPendingHighSurrogate(Terminal::Types::Input::Stream::Stdin, 0);
    }
} // namespace GameWIP::Terminal::Internal::TestHooks

namespace GameWIP::Terminal::Internal::TestHooks::Win32
{
    /// ------------------------------------------------------------
    /// Win32 key decoding
    /// ------------------------------------------------------------

    void resetWin32KeyDecoder() noexcept
    {
        Terminal::Platform::Win32::Internal::resetWin32KeyDecoder();
    }

    std::size_t consoleWaitCallCount() noexcept
    {
        return Terminal::Platform::Win32::Internal::consoleWaitCallCount();
    }

    Win32ConsoleWaitResult waitForConsoleRecordForTest(std::chrono::milliseconds timeout, const std::stop_token &stopToken)
    {
        return Terminal::Platform::Win32::Internal::waitForConsoleRecordForTest(timeout, stopToken);
    }

    Win32KeyDecodeResult decodeWin32KeyRecord(
        bool keyDown,
        std::uint16_t virtualKey,
        char16_t unicodeCharacter,
        std::uint32_t controlState,
        std::uint16_t repeatCount,
        std::uint16_t scanCode) noexcept
    {
        return Terminal::Platform::Win32::Internal::decodeWin32KeyRecord(keyDown, virtualKey, unicodeCharacter, controlState, repeatCount, scanCode);
    }

    std::optional<Terminal::Types::Event> takePendingWin32KeyEvent() noexcept
    {
        return Terminal::Platform::Win32::Internal::takePendingWin32KeyEvent();
    }

    /// ------------------------------------------------------------
    /// Input endpoint state
    /// ------------------------------------------------------------

    void setPendingHighSurrogate(Terminal::Types::Input::Stream stream, std::uint16_t surrogate) noexcept
    {
        Terminal::Platform::Win32::Internal::setPendingHighSurrogate(stream, surrogate);
    }

    bool hasPendingHighSurrogate(Terminal::Types::Input::Stream stream) noexcept
    {
        return Terminal::Platform::Win32::Internal::hasPendingHighSurrogate(stream);
    }
} // namespace GameWIP::Terminal::Internal::TestHooks::Win32
#endif
