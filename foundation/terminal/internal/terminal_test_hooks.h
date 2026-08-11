/// @file terminal_test_hooks.h
/// @brief Source-tree-only deterministic overrides, capture, and failure injection for Terminal validation.
/// @warning This header is not installed and must not be used by production consumers.

#pragma once

#include "terminal/terminal.h"

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
        bool capabilitiesOverrideEnabled = false;
        Terminal::Types::InputCapabilities capabilitiesOverride{};

        bool inputBytesOverrideEnabled = false;
        bool endOfStreamWhenInputEmpty = true;
        std::string inputBytes;

        bool inputEventsOverrideEnabled = false;
        bool endOfStreamWhenEventsEmpty = true;
        std::vector<Terminal::Types::Event> inputEvents;
        std::size_t nextInputEvent = 0;

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
        HookFailure nextInputModeFailure;
        HookFailure nextReadFailure;
        HookFailure nextTerminalSizeFailure;
        HookFailure nextCursorPositionFailure;
        HookFailure nextTextWriteFailure;
        HookFailure nextByteWriteFailure;
        HookFailure nextFlushFailure;
        HookBlock nextReadBlock;
        HookBlock nextTextWriteBlock;
    };

    /// @brief Singleton hook state; callers lock mutex before non-atomic access.
    extern TerminalTestHookState terminalTestHookState;

    /// @brief Maps the only supported input stream to its hook-state array slot.
    [[nodiscard]] std::size_t inputIndex(Terminal::Types::InputStream stream) noexcept;
    /// @brief Maps stdout or stderr to its hook-state array slot.
    [[nodiscard]] std::size_t outputIndex(Terminal::Types::OutputStream stream) noexcept;
    /// @brief Atomically consumes a one-shot forced failure and returns its portable code.
    [[nodiscard]] std::optional<IO::Types::ErrorCode> consumeFailure(HookFailure &failure) noexcept;
    /// @brief Blocks at an armed operation gate until the test releases it.
    void waitAtBlock(HookBlock &block);
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

    /// @brief Replaces deterministic structured events consumed by readEvent() and managed line editing.
    /// @param endOfStreamWhenEmpty True reports EOF after the final event; false reports WouldBlock/TimedOut.
    /// @warning Test-only API.
    GAMEWIP_TERMINAL_EXPORT void setInputEvents(
        Terminal::Types::InputStream stream,
        std::span<const Terminal::Types::Event> events,
        bool endOfStreamWhenEmpty = true);

    /// @brief Disables deterministic structured-event input.
    /// @warning Test-only API.
    GAMEWIP_TERMINAL_EXPORT void clearInputEvents(Terminal::Types::InputStream stream) noexcept;

#if defined(_WIN32)
    /// @brief Test-only mirror of native Win32 key-decoder dispositions.
    enum class Win32KeyDecodeDisposition : std::uint8_t
    {
        Produced,
        Pending,
        Ignored,
        Failed
    };

    /// @brief Test-only result returned by deterministic Win32 key-record decoding.
    struct Win32KeyDecodeResult
    {
        IO::Types::Status status;
        Win32KeyDecodeDisposition disposition = Win32KeyDecodeDisposition::Ignored;
        std::optional<Terminal::Types::Event> event;
    };

    /// @brief Clears deterministic Win32 key-down, surrogate, and pending-repeat decoder state.
    GAMEWIP_TERMINAL_EXPORT void resetWin32KeyDecoder() noexcept;

    /// @brief Decodes one synthetic Win32 KEY_EVENT_RECORD described only by portable integer fields.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT Win32KeyDecodeResult decodeWin32KeyRecord(
        bool keyDown,
        std::uint16_t virtualKey,
        char16_t unicodeCharacter = u'\0',
        std::uint32_t controlState = 0,
        std::uint16_t repeatCount = 1,
        std::uint16_t scanCode = 0) noexcept;

    /// @brief Returns a pending repeat event retained by the deterministic Win32 decoder.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT std::optional<Terminal::Types::Event> takePendingWin32KeyEvent() noexcept;
#endif

    /// @brief Seeds the native pending UTF-16 high surrogate for endpoint-replacement validation.
    /// @warning Test-only API. Available only on the Win32 validation backend.
    GAMEWIP_TERMINAL_EXPORT void setPendingHighSurrogate(Terminal::Types::InputStream stream, std::uint16_t surrogate) noexcept;

    /// @brief Returns whether the current native input endpoint retains a pending UTF-16 high surrogate.
    /// @warning Test-only API. Available only on the Win32 validation backend.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT bool hasPendingHighSurrogate(Terminal::Types::InputStream stream) noexcept;

    /// @brief Overrides internal native-mode capture/set/restore with deterministic in-memory flags.
    /// @warning Test-only API.
    GAMEWIP_TERMINAL_EXPORT void setInputModeOverride(
        Terminal::Types::InputStream stream,
        bool lineBuffered = true,
        bool echoInput = true,
        bool processControlKeys = true);

    /// @brief Returns whether the deterministic input-mode override currently matches all requested flags.
    /// @warning Test-only API.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT bool inputModeOverrideMatches(
        Terminal::Types::InputStream stream,
        bool lineBuffered,
        bool echoInput,
        bool processControlKeys) noexcept;

    /// @brief Returns whether deterministic managed-event flags match the requested state.
    /// @warning Test-only API.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT bool inputManagedEventModeOverrideMatches(
        Terminal::Types::InputStream stream,
        bool reportResizeEvents,
        bool reportPointerEvents,
        bool exclusiveEventDelivery) noexcept;

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

    /// @brief Arms the next backend read to pause after taking Terminal input serialization.
    GAMEWIP_TERMINAL_EXPORT void blockNextRead();
    /// @brief Waits until an armed backend read reaches its deterministic pause point.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT bool waitUntilReadBlocked(std::chrono::milliseconds timeout = std::chrono::seconds{5});
    /// @brief Releases a backend read paused by blockNextRead().
    GAMEWIP_TERMINAL_EXPORT void releaseBlockedRead() noexcept;

    /// @brief Arms the next backend text write to pause while holding Terminal stream serialization.
    GAMEWIP_TERMINAL_EXPORT void blockNextTextWrite();
    /// @brief Waits until an armed backend text write reaches its deterministic pause point.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT bool waitUntilTextWriteBlocked(std::chrono::milliseconds timeout = std::chrono::seconds{5});
    /// @brief Releases a backend text write paused by blockNextTextWrite().
    GAMEWIP_TERMINAL_EXPORT void releaseBlockedTextWrite() noexcept;

    /// @brief Forces the next input-capability query to fail with code.
    GAMEWIP_TERMINAL_EXPORT void forceNextInputCapabilityFailure(IO::Types::ErrorCode code = IO::Types::ErrorCode::StatFailed) noexcept;
    /// @brief Forces the next output-capability query to fail with code.
    GAMEWIP_TERMINAL_EXPORT void forceNextOutputCapabilityFailure(IO::Types::ErrorCode code = IO::Types::ErrorCode::StatFailed) noexcept;
    /// @brief Forces the next output-preparation attempt to fail with code.
    GAMEWIP_TERMINAL_EXPORT void forceNextOutputPreparationFailure(IO::Types::ErrorCode code = IO::Types::ErrorCode::NativeFailure) noexcept;
    /// @brief Forces the next internal input-mode capture/set/restore operation to fail with code.
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
