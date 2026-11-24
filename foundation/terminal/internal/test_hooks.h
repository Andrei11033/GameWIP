/// @file test_hooks.h
/// @brief Source-tree-only deterministic overrides, capture, and failure injection for Terminal validation.
/// @warning This header is not installed and must not be used by production consumers.

#pragma once

#include "terminal/internal/test_export.h"
#include "terminal/input.h"
#include "terminal/output_types.h"

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#ifndef TERMINAL_INTERNAL_TEST_HOOKS
#define TERMINAL_INTERNAL_TEST_HOOKS 0
#endif

#if TERMINAL_INTERNAL_TEST_HOOKS
namespace GameWIP::Terminal::Internal::TestHooks::State
{
    /// ------------------------------------------------------------
    /// Failure and synchronization primitives
    /// ------------------------------------------------------------

    /// @brief One-shot portable error injection consumed atomically by a backend operation.
    struct HookFailure
    {
        std::atomic_bool enabled = false;
        std::atomic<int> code = static_cast<int>(IO::Types::ErrorCode::Unknown);
    };

    /// @brief One deterministic backend-operation gate used by concurrency validation.
    struct HookBlock
    {
        std::condition_variable condition;
        bool enabled = false;
        bool reached = false;
        bool released = true;
    };

    /// @brief Mutable deterministic stdin state protected by TerminalTestHookState::mutex.
    struct InputHookState
    {
        // Capability override
        bool capabilitiesOverrideEnabled = false;
        Terminal::Types::Input::Capabilities capabilitiesOverride{};

        // Byte input
        bool inputBytesOverrideEnabled = false;
        bool endOfStreamWhenInputEmpty = true;
        std::string inputBytes;

        // Structured events
        bool inputEventsOverrideEnabled = false;
        bool endOfStreamWhenEventsEmpty = true;
        std::vector<Terminal::Types::Event> inputEvents;
        std::size_t nextInputEvent = 0;

        // Input mode
        bool inputModeOverrideEnabled = false;
        bool lineBuffered = true;
        bool echoInput = true;
        bool processControlKeys = true;
        bool reportResizeEvents = false;
        bool reportPointerEvents = false;
        bool exclusiveEventDelivery = false;
    };

    /// @brief Mutable deterministic stdout/stderr state protected by TerminalTestHookState::mutex.
    struct OutputHookState
    {
        // Capability overrides
        bool capabilitiesOverrideEnabled = false;
        Terminal::Types::Output::Capabilities capabilitiesOverride{};
        bool preparedCapabilitiesOverrideEnabled = false;
        Terminal::Types::Output::Capabilities preparedCapabilitiesOverride{};
        bool prepared = false;

        // Captured output
        bool captureEnabled = false;
        std::vector<std::byte> capturedOutput;

        // Terminal geometry
        bool terminalSizeOverrideEnabled = false;
        Terminal::Types::Size terminalSizeOverride{};

        // Cursor position
        bool cursorPositionOverrideEnabled = false;
        Terminal::Types::Cursor::Position cursorPositionOverride{};

        // Cursor rendering simulation
        bool cursorRenderingSimulationEnabled = false;
        Terminal::Types::Cursor::Position cursorRenderingPosition{};
        Terminal::Types::Cursor::Position cursorRenderingViewportOrigin{};
        std::vector<Terminal::Types::Cursor::Position> cursorRenderingSetHistory;
    };

    /// @brief Process-wide Terminal hook state shared by core and Win32 backend tests.
    struct TerminalTestHookState
    {
        // Synchronization and stream state
        std::mutex mutex;
        std::array<InputHookState, 1> inputStreams{};
        std::array<OutputHookState, 2> outputStreams{};

        // One-shot failures and operation gates
        HookFailure nextInputCapabilityFailure;
        HookFailure nextOutputCapabilityFailure;
        HookFailure nextOutputPreparationFailure;
        HookFailure nextInputModeFailure;
        HookFailure nextReadFailure;
        HookFailure nextEndpointIdentityFailure;
        HookFailure nextCancellationResetFailure;
        HookFailure nextCancellationSignalFailure;
        HookFailure nextTerminalSizeFailure;
        HookFailure nextCursorPositionFailure;
        HookFailure nextTextWriteFailure;
        HookFailure nextByteWriteFailure;
        HookFailure nextFlushFailure;
        HookBlock nextReadBlock;
        HookBlock nextTextWriteBlock;
    };

    /// @brief Singleton hook state; callers lock mutex before non-atomic access.
    extern TERMINAL_TEST_EXPORT TerminalTestHookState terminalTestHookState;

    /// @brief Maps the only supported input stream to its hook-state array slot.
    [[nodiscard]] std::size_t inputIndex(Terminal::Types::Input::Stream stream) noexcept;
    /// @brief Maps stdout or stderr to its hook-state array slot.
    [[nodiscard]] std::size_t outputIndex(Terminal::Types::Output::Stream stream) noexcept;
    /// @brief Atomically consumes a one-shot forced failure and returns its portable code.
    [[nodiscard]] TERMINAL_TEST_EXPORT std::optional<IO::Types::ErrorCode> consumeFailure(HookFailure &failure) noexcept;
    /// @brief Blocks at an armed operation gate until the test releases it.
    void waitAtBlock(HookBlock &block);
    /// @brief Restores the complete process-wide hook state to deterministic defaults.
    void resetTerminalTestHooks() noexcept;
} // namespace GameWIP::Terminal::Internal::TestHooks::State

namespace GameWIP::Terminal::Internal::TestHooks
{
    /// @brief Resets backend-owned state between validation scenarios.
    TERMINAL_TEST_EXPORT void resetBackendInputState() noexcept;

    /// ------------------------------------------------------------
    /// Capability and input overrides
    /// ------------------------------------------------------------

    /// @brief Clears all pending terminal test-hook failures, captures, and overrides.
    /// @warning Test-only API. Available only when TERMINAL_INTERNAL_TEST_HOOKS is enabled.
    TERMINAL_TEST_EXPORT void reset() noexcept;

    /// @brief Overrides reported input capabilities for a stream.
    /// @warning Test-only API. Persistent until reset.
    TERMINAL_TEST_EXPORT void setInputCapabilitiesOverride(
        Terminal::Types::Input::Stream stream,
        const Terminal::Types::Input::Capabilities &capabilities);

    /// @brief Overrides reported output capabilities for a stream.
    /// @warning Test-only API. Persistent until reset.
    TERMINAL_TEST_EXPORT void setOutputCapabilitiesOverride(
        Terminal::Types::Output::Stream stream,
        const Terminal::Types::Output::Capabilities &capabilities);

    /// @brief Overrides capabilities reported after output preparation.
    /// @warning Test-only API. Persistent until reset.
    TERMINAL_TEST_EXPORT void setPreparedOutputCapabilitiesOverride(
        Terminal::Types::Output::Stream stream,
        const Terminal::Types::Output::Capabilities &capabilities);

    /// @brief Replaces the in-memory input bytes used by read hooks.
    /// @param endOfStreamWhenEmpty True makes an empty hook stream report EOF; false reports WouldBlock/TimedOut.
    /// @throws Any allocation exception from copying bytes into hook-owned storage.
    /// @warning Test-only API.
    TERMINAL_TEST_EXPORT void setInputBytes(Terminal::Types::Input::Stream stream, std::string_view bytes, bool endOfStreamWhenEmpty = true);

    /// @brief Replaces deterministic structured events consumed by readEvent() and managed line editing.
    /// @param endOfStreamWhenEmpty True reports EOF after the final event; false reports WouldBlock/TimedOut.
    /// @warning Test-only API.
    TERMINAL_TEST_EXPORT void setInputEvents(
        Terminal::Types::Input::Stream stream,
        std::span<const Terminal::Types::Event> events,
        bool endOfStreamWhenEmpty = true);

    /// ------------------------------------------------------------
    /// Input mode and endpoint state
    /// ------------------------------------------------------------

    /// @brief Overrides internal native-mode capture/set/restore with deterministic in-memory flags.
    /// @warning Test-only API.
    TERMINAL_TEST_EXPORT void setInputModeOverride(
        Terminal::Types::Input::Stream stream,
        bool lineBuffered = true,
        bool echoInput = true,
        bool processControlKeys = true);

    /// @brief Returns whether the deterministic input-mode override matches all requested flags.
    /// @warning Test-only API.
    [[nodiscard]] TERMINAL_TEST_EXPORT bool inputModeOverrideMatches(
        Terminal::Types::Input::Stream stream,
        bool lineBuffered,
        bool echoInput,
        bool processControlKeys) noexcept;

    /// @brief Returns whether deterministic managed-event flags match the requested state.
    /// @warning Test-only API.
    [[nodiscard]] TERMINAL_TEST_EXPORT bool inputManagedEventModeOverrideMatches(
        Terminal::Types::Input::Stream stream,
        bool reportResizeEvents,
        bool reportPointerEvents,
        bool exclusiveEventDelivery) noexcept;

    /// ------------------------------------------------------------
    /// Output capture and terminal state
    /// ------------------------------------------------------------

    /// @brief Enables or disables output capture for a stream.
    /// @warning Test-only API.
    TERMINAL_TEST_EXPORT void setOutputCapture(Terminal::Types::Output::Stream stream, bool enabled) noexcept;

    /// @brief Returns captured output bytes in write order.
    /// @throws Any allocation exception from creating the returned snapshot.
    /// @warning Test-only API.
    [[nodiscard]] TERMINAL_TEST_EXPORT std::vector<std::byte> capturedOutput(Terminal::Types::Output::Stream stream);

    /// @brief Returns captured output bytes as a string for text-oriented assertions.
    /// @throws Any allocation exception from creating the returned snapshot.
    /// @warning Test-only API.
    [[nodiscard]] TERMINAL_TEST_EXPORT std::string capturedOutputText(Terminal::Types::Output::Stream stream);

    /// @brief Clears captured output for a stream.
    /// @warning Test-only API.
    TERMINAL_TEST_EXPORT void clearCapturedOutput(Terminal::Types::Output::Stream stream) noexcept;

    /// @brief Overrides terminal size query results for a stream.
    /// @warning Test-only API.
    TERMINAL_TEST_EXPORT void setTerminalSizeOverride(Terminal::Types::Output::Stream stream, Terminal::Types::Size size);

    /// @brief Overrides cursor position query results for a stream.
    /// @warning Test-only API.
    TERMINAL_TEST_EXPORT void setCursorPositionOverride(Terminal::Types::Output::Stream stream, Terminal::Types::Cursor::Position position);

    /// @brief Enables deterministic cursor advancement, wrapping, viewport scrolling, and resize reflow.
    /// @warning Test-only API. Text-cell simulation is intended for ASCII managed-line rendering fixtures.
    TERMINAL_TEST_EXPORT void enableCursorRenderingSimulation(
        Terminal::Types::Output::Stream stream,
        Terminal::Types::Size size,
        Terminal::Types::Cursor::Position position,
        Terminal::Types::Cursor::Position viewportOrigin = {});

    /// @brief Returns the simulated viewport origin after writes and resize events.
    [[nodiscard]] TERMINAL_TEST_EXPORT Terminal::Types::Cursor::Position cursorRenderingViewportOrigin(
        Terminal::Types::Output::Stream stream) noexcept;

    /// @brief Returns backend-stable positions requested by managed line redraw.
    [[nodiscard]] TERMINAL_TEST_EXPORT std::vector<Terminal::Types::Cursor::Position> cursorRenderingSetHistory(
        Terminal::Types::Output::Stream stream);

    /// ------------------------------------------------------------
    /// Concurrency gates and failure injection
    /// ------------------------------------------------------------

    /// @brief Arms the next backend read to pause after taking Terminal input serialization.
    TERMINAL_TEST_EXPORT void blockNextRead();
    /// @brief Waits until an armed backend read reaches its deterministic pause point.
    [[nodiscard]] TERMINAL_TEST_EXPORT bool waitUntilReadBlocked(std::chrono::milliseconds timeout = std::chrono::seconds{5});
    /// @brief Releases a backend read paused by blockNextRead().
    TERMINAL_TEST_EXPORT void releaseBlockedRead() noexcept;

    /// @brief Arms the next backend text write to pause while holding Terminal stream serialization.
    TERMINAL_TEST_EXPORT void blockNextTextWrite();
    /// @brief Waits until an armed backend text write reaches its deterministic pause point.
    [[nodiscard]] TERMINAL_TEST_EXPORT bool waitUntilTextWriteBlocked(std::chrono::milliseconds timeout = std::chrono::seconds{5});
    /// @brief Releases a backend text write paused by blockNextTextWrite().
    TERMINAL_TEST_EXPORT void releaseBlockedTextWrite() noexcept;

    /// @brief Forces the next input-capability query to fail with code.
    TERMINAL_TEST_EXPORT void forceNextInputCapabilityFailure(IO::Types::ErrorCode code = IO::Types::ErrorCode::StatFailed) noexcept;
    /// @brief Forces the next output-capability query to fail with code.
    TERMINAL_TEST_EXPORT void forceNextOutputCapabilityFailure(IO::Types::ErrorCode code = IO::Types::ErrorCode::StatFailed) noexcept;
    /// @brief Forces the next output-preparation attempt to fail with code.
    TERMINAL_TEST_EXPORT void forceNextOutputPreparationFailure(IO::Types::ErrorCode code = IO::Types::ErrorCode::NativeFailure) noexcept;
    /// @brief Forces the next internal input-mode capture/set/restore operation to fail with code.
    TERMINAL_TEST_EXPORT void forceNextInputModeFailure(IO::Types::ErrorCode code = IO::Types::ErrorCode::NativeFailure) noexcept;
    /// @brief Forces the next input read to fail with code.
    TERMINAL_TEST_EXPORT void forceNextReadFailure(IO::Types::ErrorCode code = IO::Types::ErrorCode::ReadFailed) noexcept;
    /// @brief Forces the next native input endpoint identity lookup to fail.
    /// @details The hook is one-shot and reports the configured portable code; reset() clears it.
    TERMINAL_TEST_EXPORT void forceNextEndpointIdentityFailure(IO::Types::ErrorCode code = IO::Types::ErrorCode::NativeFailure) noexcept;
    /// @brief Forces the next native cancellation-event reset to fail.
    /// @details The hook is one-shot and reports a native operation failure; reset() clears it.
    TERMINAL_TEST_EXPORT void forceNextCancellationResetFailure() noexcept;
    /// @brief Forces the next stop callback to skip its best-effort cancellation signal.
    /// @details The hook is one-shot; the callback remains best-effort and returns no status.
    TERMINAL_TEST_EXPORT void forceNextCancellationSignalFailure() noexcept;
    /// @brief Forces the next terminal-size query to fail with code.
    TERMINAL_TEST_EXPORT void forceNextTerminalSizeFailure(IO::Types::ErrorCode code = IO::Types::ErrorCode::StatFailed) noexcept;
    /// @brief Forces the next cursor-position query to fail with code.
    TERMINAL_TEST_EXPORT void forceNextCursorPositionFailure(IO::Types::ErrorCode code = IO::Types::ErrorCode::StatFailed) noexcept;
    /// @brief Forces the next text write to fail with code.
    TERMINAL_TEST_EXPORT void forceNextTextWriteFailure(IO::Types::ErrorCode code = IO::Types::ErrorCode::WriteFailed) noexcept;
    /// @brief Forces the next byte write to fail with code.
    TERMINAL_TEST_EXPORT void forceNextByteWriteFailure(IO::Types::ErrorCode code = IO::Types::ErrorCode::WriteFailed) noexcept;
    /// @brief Forces the next flush operation to fail with code.
    TERMINAL_TEST_EXPORT void forceNextFlushFailure(IO::Types::ErrorCode code = IO::Types::ErrorCode::FlushFailed) noexcept;
} // namespace GameWIP::Terminal::Internal::TestHooks
#endif
