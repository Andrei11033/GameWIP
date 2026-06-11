/// @file terminal_test_hooks.h
/// @brief Internal test hooks for deterministic Terminal tests.

#pragma once

#include "terminal/terminal.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#ifndef INTERNAL_TERMINAL_TEST_HOOKS
#define INTERNAL_TERMINAL_TEST_HOOKS 0
#endif

#if INTERNAL_TERMINAL_TEST_HOOKS
namespace GameWIP::Terminal::Detail::TestHooks
{
    struct HookFailure
    {
        std::atomic_bool enabled = false;
        std::atomic<int> code = static_cast<int>(IO::Types::ErrorCode::Unknown);
    };

    struct InputHookState
    {
        bool capabilitiesOverrideEnabled = false;
        Terminal::Types::InputCapabilities capabilitiesOverride{};

        bool inputBytesOverrideEnabled = false;
        bool endOfStreamWhenInputEmpty = true;
        std::string inputBytes;

        bool inputModeOverrideEnabled = false;
        Terminal::Types::InputMode currentInputMode{};
        Terminal::Types::InputMode defaultInputMode{};
    };

    struct OutputHookState
    {
        bool capabilitiesOverrideEnabled = false;
        Terminal::Types::OutputCapabilities capabilitiesOverride{};
        bool preparedCapabilitiesOverrideEnabled = false;
        Terminal::Types::OutputCapabilities preparedCapabilitiesOverride{};
        bool prepared = false;

        bool captureEnabled = false;
        std::vector<std::byte> capturedOutput;
        std::size_t preparationCalls = 0;
        std::size_t textWriteCalls = 0;

        bool terminalSizeOverrideEnabled = false;
        Terminal::Types::TerminalSize terminalSizeOverride{};

        bool cursorPositionOverrideEnabled = false;
        Terminal::Types::CursorPosition cursorPositionOverride{};
    };

    struct TerminalTestHookState
    {
        std::mutex mutex;
        std::array<InputHookState, 1> inputStreams{};
        std::array<OutputHookState, 2> outputStreams{};

        HookFailure nextInputCapabilityFailure;
        HookFailure nextOutputCapabilityFailure;
        HookFailure nextOutputPreparationFailure;
        HookFailure nextInputAvailabilityFailure;
        HookFailure nextInputModeFailure;
        HookFailure nextReadFailure;
        HookFailure nextTerminalSizeFailure;
        HookFailure nextCursorPositionFailure;
        HookFailure nextTextWriteFailure;
        HookFailure nextByteWriteFailure;
        HookFailure nextFlushFailure;
    };

    extern TerminalTestHookState terminalTestHookState;

    [[nodiscard]] std::size_t inputIndex(Terminal::Types::InputStream stream) noexcept;
    [[nodiscard]] std::size_t outputIndex(Terminal::Types::OutputStream stream) noexcept;
    [[nodiscard]] std::optional<IO::Types::ErrorCode> consumeFailure(HookFailure &failure) noexcept;
    void resetTerminalTestHooks() noexcept;
} // namespace GameWIP::Terminal::Detail::TestHooks

namespace GameWIP::Terminal::TestHooks
{
    /// @brief Clears all pending terminal test-hook failures, captures, and overrides.
    /// @warning Test-only API. Available only when INTERNAL_TERMINAL_TEST_HOOKS is enabled.
    void reset() noexcept;

    /// @brief Overrides reported input capabilities for a stream.
    /// @warning Test-only API. Persistent until reset or clearInputCapabilitiesOverride.
    void setInputCapabilitiesOverride(Terminal::Types::InputStream stream, const Terminal::Types::InputCapabilities &capabilities);

    /// @brief Clears an input capabilities override.
    /// @warning Test-only API.
    void clearInputCapabilitiesOverride(Terminal::Types::InputStream stream) noexcept;

    /// @brief Overrides reported output capabilities for a stream.
    /// @warning Test-only API. Persistent until reset or clearOutputCapabilitiesOverride.
    void setOutputCapabilitiesOverride(Terminal::Types::OutputStream stream, const Terminal::Types::OutputCapabilities &capabilities);

    /// @brief Clears an output capabilities override.
    /// @warning Test-only API.
    void clearOutputCapabilitiesOverride(Terminal::Types::OutputStream stream) noexcept;

    /// @brief Overrides capabilities reported after output preparation.
    /// @warning Test-only API. Persistent until reset or clearOutputCapabilitiesOverride.
    void setPreparedOutputCapabilitiesOverride(Terminal::Types::OutputStream stream, const Terminal::Types::OutputCapabilities &capabilities);

    /// @brief Replaces the in-memory input bytes used by read hooks.
    /// @param endOfStreamWhenEmpty True makes an empty hook stream report EOF; false reports WouldBlock/TimedOut.
    /// @warning Test-only API.
    void setInputBytes(Terminal::Types::InputStream stream, std::string_view bytes, bool endOfStreamWhenEmpty = true);

    /// @brief Appends bytes to the in-memory input stream.
    /// @warning Test-only API.
    void appendInputBytes(Terminal::Types::InputStream stream, std::string_view bytes);

    /// @brief Disables in-memory input bytes for a stream.
    /// @warning Test-only API.
    void clearInputBytes(Terminal::Types::InputStream stream) noexcept;

    /// @brief Overrides input mode operations with an in-memory mode.
    /// @warning Test-only API.
    void setInputModeOverride(Terminal::Types::InputStream stream, const Terminal::Types::InputMode &defaultMode);

    /// @brief Clears an input mode override.
    /// @warning Test-only API.
    void clearInputModeOverride(Terminal::Types::InputStream stream) noexcept;

    /// @brief Enables or disables output capture for a stream.
    /// @warning Test-only API.
    void setOutputCapture(Terminal::Types::OutputStream stream, bool enabled) noexcept;

    /// @brief Returns captured output bytes in write order.
    /// @warning Test-only API.
    [[nodiscard]] std::vector<std::byte> capturedOutput(Terminal::Types::OutputStream stream);

    /// @brief Returns captured output bytes as a string for text-oriented assertions.
    /// @warning Test-only API.
    [[nodiscard]] std::string capturedOutputText(Terminal::Types::OutputStream stream);

    /// @brief Clears captured output for a stream.
    /// @warning Test-only API.
    void clearCapturedOutput(Terminal::Types::OutputStream stream) noexcept;

    /// @brief Returns the number of output preparation calls for a stream.
    /// @warning Test-only API.
    [[nodiscard]] std::size_t outputPreparationCallCount(Terminal::Types::OutputStream stream) noexcept;

    /// @brief Returns the number of backend text-write calls for a stream.
    /// @warning Test-only API.
    [[nodiscard]] std::size_t textWriteCallCount(Terminal::Types::OutputStream stream) noexcept;

    /// @brief Overrides terminal size query results for a stream.
    /// @warning Test-only API.
    void setTerminalSizeOverride(Terminal::Types::OutputStream stream, Terminal::Types::TerminalSize size);

    /// @brief Clears a terminal size override.
    /// @warning Test-only API.
    void clearTerminalSizeOverride(Terminal::Types::OutputStream stream) noexcept;

    /// @brief Overrides cursor position query results for a stream.
    /// @warning Test-only API.
    void setCursorPositionOverride(Terminal::Types::OutputStream stream, Terminal::Types::CursorPosition position);

    /// @brief Clears a cursor position override.
    /// @warning Test-only API.
    void clearCursorPositionOverride(Terminal::Types::OutputStream stream) noexcept;

    void forceNextInputCapabilityFailure(IO::Types::ErrorCode code = IO::Types::ErrorCode::StatFailed) noexcept;
    void forceNextOutputCapabilityFailure(IO::Types::ErrorCode code = IO::Types::ErrorCode::StatFailed) noexcept;
    void forceNextOutputPreparationFailure(IO::Types::ErrorCode code = IO::Types::ErrorCode::NativeFailure) noexcept;
    void forceNextInputAvailabilityFailure(IO::Types::ErrorCode code = IO::Types::ErrorCode::StatFailed) noexcept;
    void forceNextInputModeFailure(IO::Types::ErrorCode code = IO::Types::ErrorCode::NativeFailure) noexcept;
    void forceNextReadFailure(IO::Types::ErrorCode code = IO::Types::ErrorCode::ReadFailed) noexcept;
    void forceNextTerminalSizeFailure(IO::Types::ErrorCode code = IO::Types::ErrorCode::StatFailed) noexcept;
    void forceNextCursorPositionFailure(IO::Types::ErrorCode code = IO::Types::ErrorCode::StatFailed) noexcept;
    void forceNextTextWriteFailure(IO::Types::ErrorCode code = IO::Types::ErrorCode::WriteFailed) noexcept;
    void forceNextByteWriteFailure(IO::Types::ErrorCode code = IO::Types::ErrorCode::WriteFailed) noexcept;
    void forceNextFlushFailure(IO::Types::ErrorCode code = IO::Types::ErrorCode::FlushFailed) noexcept;
} // namespace GameWIP::Terminal::TestHooks
#endif
