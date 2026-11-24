/// @file test_hooks.h
/// @brief Win32-specific deterministic test hooks for Terminal input.

#pragma once

#ifndef TERMINAL_INTERNAL_TEST_HOOKS
#define TERMINAL_INTERNAL_TEST_HOOKS 0
#endif

#if TERMINAL_INTERNAL_TEST_HOOKS
#include "io/status.h"
#include "terminal/internal/test_export.h"
#include "terminal/input.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stop_token>

namespace GameWIP::Terminal::Internal::TestHooks::Win32
{
    struct Win32KeyDecodeResult;
}

namespace GameWIP::Terminal::Platform::Win32::Internal
{
    /// ------------------------------------------------------------
    /// Win32 input state
    /// ------------------------------------------------------------

    /// @brief Clears Win32 stdin identity, pending bytes, decoder, and cancellation state.
    TERMINAL_TEST_EXPORT void resetWin32InputState() noexcept;

    /// @brief Returns native console waits attempted since the last reset.
    [[nodiscard]] TERMINAL_TEST_EXPORT std::size_t consoleWaitCallCount() noexcept;

    /// @brief Portable result from a synthetic console wait.
    struct Win32ConsoleWaitResult
    {
        GameWIP::IO::Types::Status status;
        GameWIP::Terminal::Types::Input::ReadOutcome outcome = GameWIP::Terminal::Types::Input::ReadOutcome::Completed;
    };

    /// @brief Runs the production console wait path against an unsignaled event.
    [[nodiscard]] TERMINAL_TEST_EXPORT Win32ConsoleWaitResult
    waitForConsoleRecordForTest(std::chrono::milliseconds timeout, const std::stop_token &stopToken);

    /// ------------------------------------------------------------
    /// Key decoding
    /// ------------------------------------------------------------

    TERMINAL_TEST_EXPORT void resetWin32KeyDecoder() noexcept;

    /// @brief Decodes one synthetic console key record.
    [[nodiscard]] TERMINAL_TEST_EXPORT GameWIP::Terminal::Internal::TestHooks::Win32::Win32KeyDecodeResult decodeWin32KeyRecord(
        bool keyDown,
        std::uint16_t virtualKey,
        char16_t unicodeCharacter,
        std::uint32_t controlState,
        std::uint16_t repeatCount,
        std::uint16_t scanCode) noexcept;

    /// @brief Returns a pending repeat event retained by the decoder.
    [[nodiscard]] TERMINAL_TEST_EXPORT std::optional<GameWIP::Terminal::Types::Event> takePendingWin32KeyEvent() noexcept;

    /// ------------------------------------------------------------
    /// Endpoint state
    /// ------------------------------------------------------------

    /// @brief Seeds the pending UTF-16 high surrogate for endpoint-replacement validation.
    TERMINAL_TEST_EXPORT void setPendingHighSurrogate(GameWIP::Terminal::Types::Input::Stream stream, std::uint16_t surrogate) noexcept;

    /// @brief Returns whether the current input endpoint retains a high surrogate.
    [[nodiscard]] TERMINAL_TEST_EXPORT bool hasPendingHighSurrogate(GameWIP::Terminal::Types::Input::Stream stream) noexcept;
} // namespace GameWIP::Terminal::Platform::Win32::Internal

namespace GameWIP::Terminal::Internal::TestHooks::Win32
{
    /// ------------------------------------------------------------
    /// Win32 key decoding
    /// ------------------------------------------------------------

    /// @brief Test-only mirror of native key-decoder dispositions.
    enum class Win32KeyDecodeDisposition : std::uint8_t
    {
        Produced,
        Pending,
        Ignored,
        Failed
    };

    /// @brief Result returned by deterministic Win32 key decoding.
    struct Win32KeyDecodeResult
    {
        GameWIP::IO::Types::Status status;
        Win32KeyDecodeDisposition disposition = Win32KeyDecodeDisposition::Ignored;
        std::optional<GameWIP::Terminal::Types::Event> event;
    };

    /// @brief Clears deterministic key-down, surrogate, and pending-repeat state.
    TERMINAL_TEST_EXPORT void resetWin32KeyDecoder() noexcept;

    /// @brief Returns native console waits attempted since the last reset.
    [[nodiscard]] TERMINAL_TEST_EXPORT std::size_t consoleWaitCallCount() noexcept;

    /// @brief Decodes a synthetic key record described by portable fields.
    [[nodiscard]] TERMINAL_TEST_EXPORT Win32KeyDecodeResult decodeWin32KeyRecord(
        bool keyDown,
        std::uint16_t virtualKey,
        char16_t unicodeCharacter = u'\0',
        std::uint32_t controlState = 0,
        std::uint16_t repeatCount = 1,
        std::uint16_t scanCode = 0) noexcept;

    /// @brief Returns a pending repeat event retained by the deterministic decoder.
    [[nodiscard]] TERMINAL_TEST_EXPORT std::optional<GameWIP::Terminal::Types::Event> takePendingWin32KeyEvent() noexcept;

    /// ------------------------------------------------------------
    /// Endpoint state
    /// ------------------------------------------------------------

    /// @brief Seeds the native pending high surrogate for endpoint-replacement validation.
    TERMINAL_TEST_EXPORT void setPendingHighSurrogate(GameWIP::Terminal::Types::Input::Stream stream, std::uint16_t surrogate) noexcept;

    /// @brief Returns whether the current Win32 input endpoint retains a high surrogate.
    [[nodiscard]] TERMINAL_TEST_EXPORT bool hasPendingHighSurrogate(GameWIP::Terminal::Types::Input::Stream stream) noexcept;
} // namespace GameWIP::Terminal::Internal::TestHooks::Win32
#endif
