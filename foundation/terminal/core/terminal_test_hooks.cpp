/// @file terminal_test_hooks.cpp
/// @brief Source-tree deterministic Terminal overrides, capture, counters, and one-shot failure injection.

#include "terminal/internal/terminal_test_hooks.h"
#include "terminal/internal/terminal_platform.h"

#if INTERNAL_TERMINAL_TEST_HOOKS
#include <cstring>

namespace GameWIP::Terminal::Detail::TestHooks
{
    TerminalTestHookState terminalTestHookState;

    std::size_t inputIndex([[maybe_unused]] Terminal::Types::InputStream stream) noexcept
    {
        return 0;
    }

    std::size_t outputIndex(Terminal::Types::OutputStream stream) noexcept
    {
        return stream == Terminal::Types::OutputStream::Stderr ? 1U : 0U;
    }

    std::optional<IO::Types::ErrorCode> consumeFailure(HookFailure &failure) noexcept
    {
        if (!failure.enabled.exchange(false, std::memory_order_acq_rel))
        {
            return std::nullopt;
        }

        return static_cast<IO::Types::ErrorCode>(failure.code.load(std::memory_order_acquire));
    }

    void resetTerminalTestHooks() noexcept
    {
        std::lock_guard lock(terminalTestHookState.mutex);

        for (InputHookState &state : terminalTestHookState.inputStreams)
        {
            state.capabilitiesOverrideEnabled = false;
            state.capabilitiesOverride = {};
            state.inputBytesOverrideEnabled = false;
            state.endOfStreamWhenInputEmpty = true;
            state.inputBytes.clear();
            state.inputEventsOverrideEnabled = false;
            state.endOfStreamWhenEventsEmpty = true;
            state.inputEvents.clear();
            state.nextInputEvent = 0;
            state.inputModeOverrideEnabled = false;
            state.lineBuffered = true;
            state.echoInput = true;
            state.processControlKeys = true;
            state.reportResizeEvents = false;
            state.reportPointerEvents = false;
            state.exclusiveEventDelivery = false;
        }

        for (OutputHookState &state : terminalTestHookState.outputStreams)
        {
            state.capabilitiesOverrideEnabled = false;
            state.capabilitiesOverride = {};
            state.preparedCapabilitiesOverrideEnabled = false;
            state.preparedCapabilitiesOverride = {};
            state.prepared = false;
            state.captureEnabled = false;
            state.capturedOutput.clear();
            state.preparationCalls = 0;
            state.textWriteCalls = 0;
            state.terminalSizeOverrideEnabled = false;
            state.terminalSizeOverride = {};
            state.cursorPositionOverrideEnabled = false;
            state.cursorPositionOverride = {};
        }

        terminalTestHookState.nextInputCapabilityFailure.enabled.store(false, std::memory_order_release);
        terminalTestHookState.nextOutputCapabilityFailure.enabled.store(false, std::memory_order_release);
        terminalTestHookState.nextOutputPreparationFailure.enabled.store(false, std::memory_order_release);
        terminalTestHookState.nextInputModeFailure.enabled.store(false, std::memory_order_release);
        terminalTestHookState.nextReadFailure.enabled.store(false, std::memory_order_release);
        terminalTestHookState.nextTerminalSizeFailure.enabled.store(false, std::memory_order_release);
        terminalTestHookState.nextCursorPositionFailure.enabled.store(false, std::memory_order_release);
        terminalTestHookState.nextTextWriteFailure.enabled.store(false, std::memory_order_release);
        terminalTestHookState.nextByteWriteFailure.enabled.store(false, std::memory_order_release);
        terminalTestHookState.nextFlushFailure.enabled.store(false, std::memory_order_release);
    }

    void forceFailure(HookFailure &failure, IO::Types::ErrorCode code) noexcept
    {
        failure.code.store(static_cast<int>(code), std::memory_order_release);
        failure.enabled.store(true, std::memory_order_release);
    }
} // namespace GameWIP::Terminal::Detail::TestHooks

namespace GameWIP::Terminal::TestHooks
{
    using namespace Detail::TestHooks;

    void reset() noexcept
    {
        resetTerminalTestHooks();
        Detail::Platform::TestHooks::setPendingHighSurrogate(Terminal::Types::InputStream::Stdin, 0);
#if defined(_WIN32)
        Detail::Platform::TestHooks::resetWin32KeyDecoder();
#endif
    }

#if defined(_WIN32)
    void resetWin32KeyDecoder() noexcept
    {
        Detail::Platform::TestHooks::resetWin32KeyDecoder();
    }

    Win32KeyDecodeResult decodeWin32KeyRecord(
        bool keyDown,
        std::uint16_t virtualKey,
        char16_t unicodeCharacter,
        std::uint32_t controlState,
        std::uint16_t repeatCount,
        std::uint16_t scanCode) noexcept
    {
        return Detail::Platform::TestHooks::decodeWin32KeyRecord(
            keyDown,
            virtualKey,
            unicodeCharacter,
            controlState,
            repeatCount,
            scanCode);
    }

    std::optional<Terminal::Types::Event> takePendingWin32KeyEvent() noexcept
    {
        return Detail::Platform::TestHooks::takePendingWin32KeyEvent();
    }
#endif

    void setInputCapabilitiesOverride(Terminal::Types::InputStream stream, const Terminal::Types::InputCapabilities &capabilities)
    {
        std::lock_guard lock(terminalTestHookState.mutex);
        InputHookState &state = terminalTestHookState.inputStreams[inputIndex(stream)];
        state.capabilitiesOverride = capabilities;
        state.capabilitiesOverrideEnabled = true;
    }

    void clearInputCapabilitiesOverride(Terminal::Types::InputStream stream) noexcept
    {
        std::lock_guard lock(terminalTestHookState.mutex);
        InputHookState &state = terminalTestHookState.inputStreams[inputIndex(stream)];
        state.capabilitiesOverride = {};
        state.capabilitiesOverrideEnabled = false;
    }

    void setOutputCapabilitiesOverride(Terminal::Types::OutputStream stream, const Terminal::Types::OutputCapabilities &capabilities)
    {
        std::lock_guard lock(terminalTestHookState.mutex);
        OutputHookState &state = terminalTestHookState.outputStreams[outputIndex(stream)];
        state.capabilitiesOverride = capabilities;
        state.capabilitiesOverrideEnabled = true;
        state.prepared = false;
    }

    void clearOutputCapabilitiesOverride(Terminal::Types::OutputStream stream) noexcept
    {
        std::lock_guard lock(terminalTestHookState.mutex);
        OutputHookState &state = terminalTestHookState.outputStreams[outputIndex(stream)];
        state.capabilitiesOverride = {};
        state.capabilitiesOverrideEnabled = false;
        state.preparedCapabilitiesOverride = {};
        state.preparedCapabilitiesOverrideEnabled = false;
        state.prepared = false;
    }

    void setPreparedOutputCapabilitiesOverride(Terminal::Types::OutputStream stream, const Terminal::Types::OutputCapabilities &capabilities)
    {
        std::lock_guard lock(terminalTestHookState.mutex);
        OutputHookState &state = terminalTestHookState.outputStreams[outputIndex(stream)];
        state.preparedCapabilitiesOverride = capabilities;
        state.preparedCapabilitiesOverrideEnabled = true;
    }

    void setInputBytes(Terminal::Types::InputStream stream, std::string_view bytes, bool endOfStreamWhenEmpty)
    {
        std::lock_guard lock(terminalTestHookState.mutex);
        InputHookState &state = terminalTestHookState.inputStreams[inputIndex(stream)];
        state.inputBytes.assign(bytes.data(), bytes.size());
        state.endOfStreamWhenInputEmpty = endOfStreamWhenEmpty;
        state.inputBytesOverrideEnabled = true;
    }

    void appendInputBytes(Terminal::Types::InputStream stream, std::string_view bytes)
    {
        std::lock_guard lock(terminalTestHookState.mutex);
        InputHookState &state = terminalTestHookState.inputStreams[inputIndex(stream)];
        state.inputBytes.append(bytes.data(), bytes.size());
        state.inputBytesOverrideEnabled = true;
    }

    void clearInputBytes(Terminal::Types::InputStream stream) noexcept
    {
        std::lock_guard lock(terminalTestHookState.mutex);
        InputHookState &state = terminalTestHookState.inputStreams[inputIndex(stream)];
        state.inputBytes.clear();
        state.inputBytesOverrideEnabled = false;
        state.endOfStreamWhenInputEmpty = true;
    }

    void setInputEvents(
        Terminal::Types::InputStream stream,
        std::span<const Terminal::Types::Event> events,
        bool endOfStreamWhenEmpty)
    {
        std::lock_guard lock(terminalTestHookState.mutex);
        InputHookState &state = terminalTestHookState.inputStreams[inputIndex(stream)];
        state.inputEvents.assign(events.begin(), events.end());
        state.nextInputEvent = 0;
        state.endOfStreamWhenEventsEmpty = endOfStreamWhenEmpty;
        state.inputEventsOverrideEnabled = true;
    }

    void clearInputEvents(Terminal::Types::InputStream stream) noexcept
    {
        std::lock_guard lock(terminalTestHookState.mutex);
        InputHookState &state = terminalTestHookState.inputStreams[inputIndex(stream)];
        state.inputEvents.clear();
        state.nextInputEvent = 0;
        state.endOfStreamWhenEventsEmpty = true;
        state.inputEventsOverrideEnabled = false;
    }

    void setPendingHighSurrogate(Terminal::Types::InputStream stream, std::uint16_t surrogate) noexcept
    {
        Detail::Platform::TestHooks::setPendingHighSurrogate(stream, surrogate);
    }

    bool hasPendingHighSurrogate(Terminal::Types::InputStream stream) noexcept
    {
        return Detail::Platform::TestHooks::hasPendingHighSurrogate(stream);
    }

    void setInputModeOverride(Terminal::Types::InputStream stream, bool lineBuffered, bool echoInput, bool processControlKeys)
    {
        std::lock_guard lock(terminalTestHookState.mutex);
        InputHookState &state = terminalTestHookState.inputStreams[inputIndex(stream)];
        state.lineBuffered = lineBuffered;
        state.echoInput = echoInput;
        state.processControlKeys = processControlKeys;
        state.inputModeOverrideEnabled = true;
    }

    bool inputModeOverrideMatches(Terminal::Types::InputStream stream, bool lineBuffered, bool echoInput, bool processControlKeys) noexcept
    {
        std::lock_guard lock(terminalTestHookState.mutex);
        const InputHookState &state = terminalTestHookState.inputStreams[inputIndex(stream)];
        return state.inputModeOverrideEnabled && state.lineBuffered == lineBuffered && state.echoInput == echoInput &&
               state.processControlKeys == processControlKeys;
    }

    bool inputManagedEventModeOverrideMatches(
        Terminal::Types::InputStream stream,
        bool reportResizeEvents,
        bool reportPointerEvents,
        bool exclusiveEventDelivery) noexcept
    {
        std::lock_guard lock(terminalTestHookState.mutex);
        const InputHookState &state = terminalTestHookState.inputStreams[inputIndex(stream)];
        return state.inputModeOverrideEnabled && state.reportResizeEvents == reportResizeEvents &&
               state.reportPointerEvents == reportPointerEvents && state.exclusiveEventDelivery == exclusiveEventDelivery;
    }

    void clearInputModeOverride(Terminal::Types::InputStream stream) noexcept
    {
        std::lock_guard lock(terminalTestHookState.mutex);
        InputHookState &state = terminalTestHookState.inputStreams[inputIndex(stream)];
        state.lineBuffered = true;
        state.echoInput = true;
        state.processControlKeys = true;
        state.reportResizeEvents = false;
        state.reportPointerEvents = false;
        state.exclusiveEventDelivery = false;
        state.inputModeOverrideEnabled = false;
    }

    void setOutputCapture(Terminal::Types::OutputStream stream, bool enabled) noexcept
    {
        std::lock_guard lock(terminalTestHookState.mutex);
        terminalTestHookState.outputStreams[outputIndex(stream)].captureEnabled = enabled;
    }

    std::vector<std::byte> capturedOutput(Terminal::Types::OutputStream stream)
    {
        std::lock_guard lock(terminalTestHookState.mutex);
        return terminalTestHookState.outputStreams[outputIndex(stream)].capturedOutput;
    }

    std::string capturedOutputText(Terminal::Types::OutputStream stream)
    {
        std::lock_guard lock(terminalTestHookState.mutex);
        const std::vector<std::byte> &bytes = terminalTestHookState.outputStreams[outputIndex(stream)].capturedOutput;
        std::string text(bytes.size(), '\0');
        if (!bytes.empty())
        {
            std::memcpy(text.data(), bytes.data(), bytes.size());
        }
        return text;
    }

    void clearCapturedOutput(Terminal::Types::OutputStream stream) noexcept
    {
        std::lock_guard lock(terminalTestHookState.mutex);
        terminalTestHookState.outputStreams[outputIndex(stream)].capturedOutput.clear();
    }

    std::size_t outputPreparationCallCount(Terminal::Types::OutputStream stream) noexcept
    {
        std::lock_guard lock(terminalTestHookState.mutex);
        return terminalTestHookState.outputStreams[outputIndex(stream)].preparationCalls;
    }

    std::size_t textWriteCallCount(Terminal::Types::OutputStream stream) noexcept
    {
        std::lock_guard lock(terminalTestHookState.mutex);
        return terminalTestHookState.outputStreams[outputIndex(stream)].textWriteCalls;
    }

    void setTerminalSizeOverride(Terminal::Types::OutputStream stream, Terminal::Types::TerminalSize size)
    {
        std::lock_guard lock(terminalTestHookState.mutex);
        OutputHookState &state = terminalTestHookState.outputStreams[outputIndex(stream)];
        state.terminalSizeOverride = size;
        state.terminalSizeOverrideEnabled = true;
    }

    void clearTerminalSizeOverride(Terminal::Types::OutputStream stream) noexcept
    {
        std::lock_guard lock(terminalTestHookState.mutex);
        OutputHookState &state = terminalTestHookState.outputStreams[outputIndex(stream)];
        state.terminalSizeOverride = {};
        state.terminalSizeOverrideEnabled = false;
    }

    void setCursorPositionOverride(Terminal::Types::OutputStream stream, Terminal::Types::CursorPosition position)
    {
        std::lock_guard lock(terminalTestHookState.mutex);
        OutputHookState &state = terminalTestHookState.outputStreams[outputIndex(stream)];
        state.cursorPositionOverride = position;
        state.cursorPositionOverrideEnabled = true;
    }

    void clearCursorPositionOverride(Terminal::Types::OutputStream stream) noexcept
    {
        std::lock_guard lock(terminalTestHookState.mutex);
        OutputHookState &state = terminalTestHookState.outputStreams[outputIndex(stream)];
        state.cursorPositionOverride = {};
        state.cursorPositionOverrideEnabled = false;
    }

    void forceNextInputCapabilityFailure(IO::Types::ErrorCode code) noexcept
    {
        forceFailure(terminalTestHookState.nextInputCapabilityFailure, code);
    }

    void forceNextOutputCapabilityFailure(IO::Types::ErrorCode code) noexcept
    {
        forceFailure(terminalTestHookState.nextOutputCapabilityFailure, code);
    }

    void forceNextOutputPreparationFailure(IO::Types::ErrorCode code) noexcept
    {
        forceFailure(terminalTestHookState.nextOutputPreparationFailure, code);
    }

    void forceNextInputModeFailure(IO::Types::ErrorCode code) noexcept
    {
        forceFailure(terminalTestHookState.nextInputModeFailure, code);
    }

    void forceNextReadFailure(IO::Types::ErrorCode code) noexcept
    {
        forceFailure(terminalTestHookState.nextReadFailure, code);
    }

    void forceNextTerminalSizeFailure(IO::Types::ErrorCode code) noexcept
    {
        forceFailure(terminalTestHookState.nextTerminalSizeFailure, code);
    }

    void forceNextCursorPositionFailure(IO::Types::ErrorCode code) noexcept
    {
        forceFailure(terminalTestHookState.nextCursorPositionFailure, code);
    }

    void forceNextTextWriteFailure(IO::Types::ErrorCode code) noexcept
    {
        forceFailure(terminalTestHookState.nextTextWriteFailure, code);
    }

    void forceNextByteWriteFailure(IO::Types::ErrorCode code) noexcept
    {
        forceFailure(terminalTestHookState.nextByteWriteFailure, code);
    }

    void forceNextFlushFailure(IO::Types::ErrorCode code) noexcept
    {
        forceFailure(terminalTestHookState.nextFlushFailure, code);
    }
} // namespace GameWIP::Terminal::TestHooks
#endif
