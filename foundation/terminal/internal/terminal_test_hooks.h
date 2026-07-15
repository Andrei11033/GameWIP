/// @file terminal_test_hooks.h
/// @brief Source-tree-only deterministic overrides, capture, and failure injection for Terminal validation.
/// @warning This header is not installed and must not be used by production consumers.

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
    /// @brief One-shot portable error injection consumed atomically by a backend operation.
    struct HookFailure
    {
        std::atomic_bool enabled = false;
        std::atomic<int> code = static_cast<int>(IO::Types::ErrorCode::Unknown);
    };

    /// @brief Mutable deterministic stdin state protected by TerminalTestHookState::mutex.
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

    /// @brief Mutable deterministic stdout/stderr state protected by TerminalTestHookState::mutex.
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

    /// @brief Process-wide Terminal hook state shared by core and Win32 backend tests.
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

    /// @brief Singleton hook state; callers lock mutex before non-atomic access.
    extern TerminalTestHookState terminalTestHookState;

    /// @brief Maps the only supported input stream to its hook-state array slot.
    [[nodiscard]] std::size_t inputIndex(Terminal::Types::InputStream stream) noexcept;
    /// @brief Maps stdout or stderr to its hook-state array slot.
    [[nodiscard]] std::size_t outputIndex(Terminal::Types::OutputStream stream) noexcept;
    /// @brief Atomically consumes a one-shot forced failure and returns its portable code.
    [[nodiscard]] std::optional<IO::Types::ErrorCode> consumeFailure(HookFailure &failure) noexcept;
    /// @brief Restores the complete process-wide hook state to deterministic defaults.
    void resetTerminalTestHooks() noexcept;
} // namespace GameWIP::Terminal::Detail::TestHooks

namespace GameWIP::Terminal::TestHooks
{
    /// @brief Clears all pending terminal test-hook failures, captures, and overrides.
    /// @warning Test-only API. Available only when INTERNAL_TERMINAL_TEST_HOOKS is enabled.
    GAMEWIP_TERMINAL_EXPORT void reset() noexcept;

    /// @brief Overrides reported input capabilities for a stream.
    /// @warning Test-only API. Persistent until reset or clearInputCapabilitiesOverride.
    GAMEWIP_TERMINAL_EXPORT void setInputCapabilitiesOverride(
        Terminal::Types::InputStream stream,
        const Terminal::Types::InputCapabilities &capabilities);

    /// @brief Clears an input capabilities override.
    /// @warning Test-only API.
    GAMEWIP_TERMINAL_EXPORT void clearInputCapabilitiesOverride(Terminal::Types::InputStream stream) noexcept;

    /// @brief Overrides reported output capabilities for a stream.
    /// @warning Test-only API. Persistent until reset or clearOutputCapabilitiesOverride.
    GAMEWIP_TERMINAL_EXPORT void setOutputCapabilitiesOverride(
        Terminal::Types::OutputStream stream,
        const Terminal::Types::OutputCapabilities &capabilities);

    /// @brief Clears an output capabilities override.
    /// @warning Test-only API.
    GAMEWIP_TERMINAL_EXPORT void clearOutputCapabilitiesOverride(Terminal::Types::OutputStream stream) noexcept;

    /// @brief Overrides capabilities reported after output preparation.
    /// @warning Test-only API. Persistent until reset or clearOutputCapabilitiesOverride.
    GAMEWIP_TERMINAL_EXPORT void setPreparedOutputCapabilitiesOverride(
        Terminal::Types::OutputStream stream,
        const Terminal::Types::OutputCapabilities &capabilities);

    /// @brief Replaces the in-memory input bytes used by read hooks.
    /// @param endOfStreamWhenEmpty True makes an empty hook stream report EOF; false reports WouldBlock/TimedOut.
    /// @throws Any allocation exception from copying bytes into hook-owned storage.
    /// @warning Test-only API.
    GAMEWIP_TERMINAL_EXPORT void setInputBytes(Terminal::Types::InputStream stream, std::string_view bytes, bool endOfStreamWhenEmpty = true);

    /// @brief Appends bytes to the in-memory input stream.
    /// @throws Any allocation exception from extending hook-owned storage.
    /// @warning Test-only API.
    GAMEWIP_TERMINAL_EXPORT void appendInputBytes(Terminal::Types::InputStream stream, std::string_view bytes);

    /// @brief Disables in-memory input bytes for a stream.
    /// @warning Test-only API.
    GAMEWIP_TERMINAL_EXPORT void clearInputBytes(Terminal::Types::InputStream stream) noexcept;

    /// @brief Seeds the native pending UTF-16 high surrogate for endpoint-replacement validation.
    /// @warning Test-only API. Available only on the Win32 validation backend.
    GAMEWIP_TERMINAL_EXPORT void setPendingHighSurrogate(Terminal::Types::InputStream stream, std::uint16_t surrogate) noexcept;

    /// @brief Returns whether the current native input endpoint retains a pending UTF-16 high surrogate.
    /// @warning Test-only API. Available only on the Win32 validation backend.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT bool hasPendingHighSurrogate(Terminal::Types::InputStream stream) noexcept;

    /// @brief Overrides input mode operations with an in-memory mode.
    /// @warning Test-only API.
    GAMEWIP_TERMINAL_EXPORT void setInputModeOverride(Terminal::Types::InputStream stream, const Terminal::Types::InputMode &defaultMode);

    /// @brief Clears an input mode override.
    /// @warning Test-only API.
    GAMEWIP_TERMINAL_EXPORT void clearInputModeOverride(Terminal::Types::InputStream stream) noexcept;

    /// @brief Enables or disables output capture for a stream.
    /// @warning Test-only API.
    GAMEWIP_TERMINAL_EXPORT void setOutputCapture(Terminal::Types::OutputStream stream, bool enabled) noexcept;

    /// @brief Returns captured output bytes in write order.
    /// @throws Any allocation exception from creating the returned snapshot.
    /// @warning Test-only API.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT std::vector<std::byte> capturedOutput(Terminal::Types::OutputStream stream);

    /// @brief Returns captured output bytes as a string for text-oriented assertions.
    /// @throws Any allocation exception from creating the returned snapshot.
    /// @warning Test-only API.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT std::string capturedOutputText(Terminal::Types::OutputStream stream);

    /// @brief Clears captured output for a stream.
    /// @warning Test-only API.
    GAMEWIP_TERMINAL_EXPORT void clearCapturedOutput(Terminal::Types::OutputStream stream) noexcept;

    /// @brief Returns the number of output preparation calls for a stream.
    /// @warning Test-only API.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT std::size_t outputPreparationCallCount(Terminal::Types::OutputStream stream) noexcept;

    /// @brief Returns the number of backend text-write calls for a stream.
    /// @warning Test-only API.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT std::size_t textWriteCallCount(Terminal::Types::OutputStream stream) noexcept;

    /// @brief Overrides terminal size query results for a stream.
    /// @warning Test-only API.
    GAMEWIP_TERMINAL_EXPORT void setTerminalSizeOverride(Terminal::Types::OutputStream stream, Terminal::Types::TerminalSize size);

    /// @brief Clears a terminal size override.
    /// @warning Test-only API.
    GAMEWIP_TERMINAL_EXPORT void clearTerminalSizeOverride(Terminal::Types::OutputStream stream) noexcept;

    /// @brief Overrides cursor position query results for a stream.
    /// @warning Test-only API.
    GAMEWIP_TERMINAL_EXPORT void setCursorPositionOverride(Terminal::Types::OutputStream stream, Terminal::Types::CursorPosition position);

    /// @brief Clears a cursor position override.
    /// @warning Test-only API.
    GAMEWIP_TERMINAL_EXPORT void clearCursorPositionOverride(Terminal::Types::OutputStream stream) noexcept;

    /// @brief Forces the next input-capability query to fail with code.
    GAMEWIP_TERMINAL_EXPORT void forceNextInputCapabilityFailure(IO::Types::ErrorCode code = IO::Types::ErrorCode::StatFailed) noexcept;
    /// @brief Forces the next output-capability query to fail with code.
    GAMEWIP_TERMINAL_EXPORT void forceNextOutputCapabilityFailure(IO::Types::ErrorCode code = IO::Types::ErrorCode::StatFailed) noexcept;
    /// @brief Forces the next output-preparation attempt to fail with code.
    GAMEWIP_TERMINAL_EXPORT void forceNextOutputPreparationFailure(IO::Types::ErrorCode code = IO::Types::ErrorCode::NativeFailure) noexcept;
    /// @brief Forces the next input-availability query to fail with code.
    GAMEWIP_TERMINAL_EXPORT void forceNextInputAvailabilityFailure(IO::Types::ErrorCode code = IO::Types::ErrorCode::StatFailed) noexcept;
    /// @brief Forces the next input-mode operation to fail with code.
    GAMEWIP_TERMINAL_EXPORT void forceNextInputModeFailure(IO::Types::ErrorCode code = IO::Types::ErrorCode::NativeFailure) noexcept;
    /// @brief Forces the next input read to fail with code.
    GAMEWIP_TERMINAL_EXPORT void forceNextReadFailure(IO::Types::ErrorCode code = IO::Types::ErrorCode::ReadFailed) noexcept;
    /// @brief Forces the next terminal-size query to fail with code.
    GAMEWIP_TERMINAL_EXPORT void forceNextTerminalSizeFailure(IO::Types::ErrorCode code = IO::Types::ErrorCode::StatFailed) noexcept;
    /// @brief Forces the next cursor-position query to fail with code.
    GAMEWIP_TERMINAL_EXPORT void forceNextCursorPositionFailure(IO::Types::ErrorCode code = IO::Types::ErrorCode::StatFailed) noexcept;
    /// @brief Forces the next text write to fail with code.
    GAMEWIP_TERMINAL_EXPORT void forceNextTextWriteFailure(IO::Types::ErrorCode code = IO::Types::ErrorCode::WriteFailed) noexcept;
    /// @brief Forces the next byte write to fail with code.
    GAMEWIP_TERMINAL_EXPORT void forceNextByteWriteFailure(IO::Types::ErrorCode code = IO::Types::ErrorCode::WriteFailed) noexcept;
    /// @brief Forces the next flush operation to fail with code.
    GAMEWIP_TERMINAL_EXPORT void forceNextFlushFailure(IO::Types::ErrorCode code = IO::Types::ErrorCode::FlushFailed) noexcept;
} // namespace GameWIP::Terminal::TestHooks
#endif
