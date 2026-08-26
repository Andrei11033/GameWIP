/// @file terminal_test_hooks.cpp
/// @brief Source-tree deterministic Terminal overrides, capture, counters, and one-shot failure injection.

#include "terminal/internal/terminal_test_hooks.h"
#include "terminal/internal/terminal_platform.h"

#if TERMINAL_INTERNAL_TEST_HOOKS
#include <algorithm>
#include <cstddef>

namespace GameWIP::Terminal::Detail::TestHooks
{
    TerminalTestHookState terminalTestHookState;

    std::size_t inputIndex([[maybe_unused]] Terminal::Types::Input::Stream stream) noexcept
    {
        return 0;
    }

    std::size_t outputIndex(Terminal::Types::Output::Stream stream) noexcept
    {
        return stream == Terminal::Types::Output::Stream::Stderr ? 1U : 0U;
    }

    std::optional<IO::Types::ErrorCode> consumeFailure(HookFailure &failure) noexcept
    {
        if (!failure.enabled.exchange(false, std::memory_order_acq_rel))
        {
            return std::nullopt;
        }

        return static_cast<IO::Types::ErrorCode>(failure.code.load(std::memory_order_acquire));
    }

    void waitAtBlock(HookBlock &block)
    {
        std::unique_lock lock(terminalTestHookState.mutex);
        if (!block.enabled)
        {
            return;
        }

        block.enabled = false;
        block.reached = true;
        block.released = false;
        block.condition.notify_all();
        block.condition.wait(
            lock,
            [&block]
            {
                return block.released;
            });
        block.reached = false;
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
            state.cursorRenderingSimulationEnabled = false;
            state.cursorRenderingPosition = {};
            state.cursorRenderingViewportOrigin = {};
            state.cursorRenderingSetHistory.clear();
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
        terminalTestHookState.nextReadBlock.enabled = false;
        terminalTestHookState.nextReadBlock.released = true;
        terminalTestHookState.nextReadBlock.condition.notify_all();
        terminalTestHookState.nextTextWriteBlock.enabled = false;
        terminalTestHookState.nextTextWriteBlock.released = true;
        terminalTestHookState.nextTextWriteBlock.condition.notify_all();
    }

    namespace
    {
        void forceFailure(HookFailure &failure, IO::Types::ErrorCode code) noexcept
        {
            failure.code.store(static_cast<int>(code), std::memory_order_release);
            failure.enabled.store(true, std::memory_order_release);
        }
    } // namespace
} // namespace GameWIP::Terminal::Detail::TestHooks

namespace GameWIP::Terminal::TestHooks
{
    using namespace Detail::TestHooks;

    void reset() noexcept
    {
        resetTerminalTestHooks();
        Detail::Platform::TestHooks::setPendingHighSurrogate(Terminal::Types::Input::Stream::Stdin, 0);
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
        return Detail::Platform::TestHooks::decodeWin32KeyRecord(keyDown, virtualKey, unicodeCharacter, controlState, repeatCount, scanCode);
    }

    std::optional<Terminal::Types::Event> takePendingWin32KeyEvent() noexcept
    {
        return Detail::Platform::TestHooks::takePendingWin32KeyEvent();
    }
#endif

    void setInputCapabilitiesOverride(Terminal::Types::Input::Stream stream, const Terminal::Types::Input::Capabilities &capabilities)
    {
        std::lock_guard lock(terminalTestHookState.mutex);
        InputHookState &state = terminalTestHookState.inputStreams[inputIndex(stream)];
        state.capabilitiesOverride = capabilities;
        state.capabilitiesOverrideEnabled = true;
    }

    void clearInputCapabilitiesOverride(Terminal::Types::Input::Stream stream) noexcept
    {
        std::lock_guard lock(terminalTestHookState.mutex);
        InputHookState &state = terminalTestHookState.inputStreams[inputIndex(stream)];
        state.capabilitiesOverride = {};
        state.capabilitiesOverrideEnabled = false;
    }

    void setOutputCapabilitiesOverride(Terminal::Types::Output::Stream stream, const Terminal::Types::Output::Capabilities &capabilities)
    {
        std::lock_guard lock(terminalTestHookState.mutex);
        OutputHookState &state = terminalTestHookState.outputStreams[outputIndex(stream)];
        state.capabilitiesOverride = capabilities;
        state.capabilitiesOverrideEnabled = true;
        state.prepared = false;
    }

    void clearOutputCapabilitiesOverride(Terminal::Types::Output::Stream stream) noexcept
    {
        std::lock_guard lock(terminalTestHookState.mutex);
        OutputHookState &state = terminalTestHookState.outputStreams[outputIndex(stream)];
        state.capabilitiesOverride = {};
        state.capabilitiesOverrideEnabled = false;
        state.preparedCapabilitiesOverride = {};
        state.preparedCapabilitiesOverrideEnabled = false;
        state.prepared = false;
    }

    void setPreparedOutputCapabilitiesOverride(Terminal::Types::Output::Stream stream, const Terminal::Types::Output::Capabilities &capabilities)
    {
        std::lock_guard lock(terminalTestHookState.mutex);
        OutputHookState &state = terminalTestHookState.outputStreams[outputIndex(stream)];
        state.preparedCapabilitiesOverride = capabilities;
        state.preparedCapabilitiesOverrideEnabled = true;
    }

    void setInputBytes(Terminal::Types::Input::Stream stream, std::string_view bytes, bool endOfStreamWhenEmpty)
    {
        std::lock_guard lock(terminalTestHookState.mutex);
        InputHookState &state = terminalTestHookState.inputStreams[inputIndex(stream)];
        state.inputBytes.assign(bytes.data(), bytes.size());
        state.endOfStreamWhenInputEmpty = endOfStreamWhenEmpty;
        state.inputBytesOverrideEnabled = true;
    }

    void appendInputBytes(Terminal::Types::Input::Stream stream, std::string_view bytes)
    {
        std::lock_guard lock(terminalTestHookState.mutex);
        InputHookState &state = terminalTestHookState.inputStreams[inputIndex(stream)];
        state.inputBytes.append(bytes.data(), bytes.size());
        state.inputBytesOverrideEnabled = true;
    }

    void clearInputBytes(Terminal::Types::Input::Stream stream) noexcept
    {
        std::lock_guard lock(terminalTestHookState.mutex);
        InputHookState &state = terminalTestHookState.inputStreams[inputIndex(stream)];
        state.inputBytes.clear();
        state.inputBytesOverrideEnabled = false;
        state.endOfStreamWhenInputEmpty = true;
    }

    void setInputEvents(Terminal::Types::Input::Stream stream, std::span<const Terminal::Types::Event> events, bool endOfStreamWhenEmpty)
    {
        std::lock_guard lock(terminalTestHookState.mutex);
        InputHookState &state = terminalTestHookState.inputStreams[inputIndex(stream)];
        state.inputEvents.assign(events.begin(), events.end());
        state.nextInputEvent = 0;
        state.endOfStreamWhenEventsEmpty = endOfStreamWhenEmpty;
        state.inputEventsOverrideEnabled = true;
    }

    void clearInputEvents(Terminal::Types::Input::Stream stream) noexcept
    {
        std::lock_guard lock(terminalTestHookState.mutex);
        InputHookState &state = terminalTestHookState.inputStreams[inputIndex(stream)];
        state.inputEvents.clear();
        state.nextInputEvent = 0;
        state.endOfStreamWhenEventsEmpty = true;
        state.inputEventsOverrideEnabled = false;
    }

    void setPendingHighSurrogate(Terminal::Types::Input::Stream stream, std::uint16_t surrogate) noexcept
    {
        Detail::Platform::TestHooks::setPendingHighSurrogate(stream, surrogate);
    }

    bool hasPendingHighSurrogate(Terminal::Types::Input::Stream stream) noexcept
    {
        return Detail::Platform::TestHooks::hasPendingHighSurrogate(stream);
    }

    void setInputModeOverride(Terminal::Types::Input::Stream stream, bool lineBuffered, bool echoInput, bool processControlKeys)
    {
        std::lock_guard lock(terminalTestHookState.mutex);
        InputHookState &state = terminalTestHookState.inputStreams[inputIndex(stream)];
        state.lineBuffered = lineBuffered;
        state.echoInput = echoInput;
        state.processControlKeys = processControlKeys;
        state.inputModeOverrideEnabled = true;
    }

    bool inputModeOverrideMatches(Terminal::Types::Input::Stream stream, bool lineBuffered, bool echoInput, bool processControlKeys) noexcept
    {
        std::lock_guard lock(terminalTestHookState.mutex);
        const InputHookState &state = terminalTestHookState.inputStreams[inputIndex(stream)];
        return state.inputModeOverrideEnabled && state.lineBuffered == lineBuffered && state.echoInput == echoInput &&
               state.processControlKeys == processControlKeys;
    }

    bool inputManagedEventModeOverrideMatches(
        Terminal::Types::Input::Stream stream,
        bool reportResizeEvents,
        bool reportPointerEvents,
        bool exclusiveEventDelivery) noexcept
    {
        std::lock_guard lock(terminalTestHookState.mutex);
        const InputHookState &state = terminalTestHookState.inputStreams[inputIndex(stream)];
        return state.inputModeOverrideEnabled && state.reportResizeEvents == reportResizeEvents && state.reportPointerEvents == reportPointerEvents &&
               state.exclusiveEventDelivery == exclusiveEventDelivery;
    }

    void clearInputModeOverride(Terminal::Types::Input::Stream stream) noexcept
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

    void setOutputCapture(Terminal::Types::Output::Stream stream, bool enabled) noexcept
    {
        std::lock_guard lock(terminalTestHookState.mutex);
        terminalTestHookState.outputStreams[outputIndex(stream)].captureEnabled = enabled;
    }

    std::vector<std::byte> capturedOutput(Terminal::Types::Output::Stream stream)
    {
        std::lock_guard lock(terminalTestHookState.mutex);
        return terminalTestHookState.outputStreams[outputIndex(stream)].capturedOutput;
    }

    std::string capturedOutputText(Terminal::Types::Output::Stream stream)
    {
        std::lock_guard lock(terminalTestHookState.mutex);
        const std::vector<std::byte> &bytes = terminalTestHookState.outputStreams[outputIndex(stream)].capturedOutput;
        std::string text(bytes.size(), '\0');
        if (!bytes.empty())
        {
            std::ranges::transform(
                bytes,
                text.begin(),
                [](std::byte byte)
                {
                    return std::to_integer<char>(byte);
                });
        }
        return text;
    }

    void clearCapturedOutput(Terminal::Types::Output::Stream stream) noexcept
    {
        std::lock_guard lock(terminalTestHookState.mutex);
        terminalTestHookState.outputStreams[outputIndex(stream)].capturedOutput.clear();
    }

    std::size_t outputPreparationCallCount(Terminal::Types::Output::Stream stream) noexcept
    {
        std::lock_guard lock(terminalTestHookState.mutex);
        return terminalTestHookState.outputStreams[outputIndex(stream)].preparationCalls;
    }

    std::size_t textWriteCallCount(Terminal::Types::Output::Stream stream) noexcept
    {
        std::lock_guard lock(terminalTestHookState.mutex);
        return terminalTestHookState.outputStreams[outputIndex(stream)].textWriteCalls;
    }

    void setTerminalSizeOverride(Terminal::Types::Output::Stream stream, Terminal::Types::Size size)
    {
        std::lock_guard lock(terminalTestHookState.mutex);
        OutputHookState &state = terminalTestHookState.outputStreams[outputIndex(stream)];
        state.terminalSizeOverride = size;
        state.terminalSizeOverrideEnabled = true;
    }

    void clearTerminalSizeOverride(Terminal::Types::Output::Stream stream) noexcept
    {
        std::lock_guard lock(terminalTestHookState.mutex);
        OutputHookState &state = terminalTestHookState.outputStreams[outputIndex(stream)];
        state.terminalSizeOverride = {};
        state.terminalSizeOverrideEnabled = false;
    }

    void setCursorPositionOverride(Terminal::Types::Output::Stream stream, Terminal::Types::Cursor::Position position)
    {
        std::lock_guard lock(terminalTestHookState.mutex);
        OutputHookState &state = terminalTestHookState.outputStreams[outputIndex(stream)];
        state.cursorPositionOverride = position;
        state.cursorPositionOverrideEnabled = true;
    }

    void clearCursorPositionOverride(Terminal::Types::Output::Stream stream) noexcept
    {
        std::lock_guard lock(terminalTestHookState.mutex);
        OutputHookState &state = terminalTestHookState.outputStreams[outputIndex(stream)];
        state.cursorPositionOverride = {};
        state.cursorPositionOverrideEnabled = false;
    }

    void enableCursorRenderingSimulation(
        Terminal::Types::Output::Stream stream,
        Terminal::Types::Size size,
        Terminal::Types::Cursor::Position position,
        Terminal::Types::Cursor::Position viewportOrigin)
    {
        std::lock_guard lock(terminalTestHookState.mutex);
        OutputHookState &state = terminalTestHookState.outputStreams[outputIndex(stream)];
        state.terminalSizeOverrideEnabled = true;
        state.terminalSizeOverride = size;
        state.cursorRenderingSimulationEnabled = true;
        state.cursorRenderingPosition = position;
        state.cursorRenderingViewportOrigin = viewportOrigin;
        state.cursorRenderingSetHistory.clear();
    }

    Terminal::Types::Cursor::Position cursorRenderingViewportOrigin(Terminal::Types::Output::Stream stream) noexcept
    {
        std::lock_guard lock(terminalTestHookState.mutex);
        return terminalTestHookState.outputStreams[outputIndex(stream)].cursorRenderingViewportOrigin;
    }

    std::vector<Terminal::Types::Cursor::Position> cursorRenderingSetHistory(Terminal::Types::Output::Stream stream)
    {
        std::lock_guard lock(terminalTestHookState.mutex);
        return terminalTestHookState.outputStreams[outputIndex(stream)].cursorRenderingSetHistory;
    }

    void blockNextRead()
    {
        std::lock_guard lock(terminalTestHookState.mutex);
        HookBlock &block = terminalTestHookState.nextReadBlock;
        block.enabled = true;
        block.reached = false;
        block.released = false;
    }

    bool waitUntilReadBlocked(std::chrono::milliseconds timeout)
    {
        std::unique_lock lock(terminalTestHookState.mutex);
        HookBlock &block = terminalTestHookState.nextReadBlock;
        return block.condition.wait_for(
            lock,
            timeout,
            []
            {
                return terminalTestHookState.nextReadBlock.reached;
            });
    }

    void releaseBlockedRead() noexcept
    {
        try
        {
            std::lock_guard lock(terminalTestHookState.mutex);
            terminalTestHookState.nextReadBlock.released = true;
            terminalTestHookState.nextReadBlock.condition.notify_all();
        }
        catch (...)
        {
            // Test cleanup is best effort at this noexcept synchronization boundary.
            return;
        }
    }

    void blockNextTextWrite()
    {
        std::lock_guard lock(terminalTestHookState.mutex);
        HookBlock &block = terminalTestHookState.nextTextWriteBlock;
        block.enabled = true;
        block.reached = false;
        block.released = false;
    }

    bool waitUntilTextWriteBlocked(std::chrono::milliseconds timeout)
    {
        std::unique_lock lock(terminalTestHookState.mutex);
        HookBlock &block = terminalTestHookState.nextTextWriteBlock;
        return block.condition.wait_for(
            lock,
            timeout,
            []
            {
                return terminalTestHookState.nextTextWriteBlock.reached;
            });
    }

    void releaseBlockedTextWrite() noexcept
    {
        try
        {
            std::lock_guard lock(terminalTestHookState.mutex);
            terminalTestHookState.nextTextWriteBlock.released = true;
            terminalTestHookState.nextTextWriteBlock.condition.notify_all();
        }
        catch (...)
        {
            // Test cleanup is best effort at this noexcept synchronization boundary.
            return;
        }
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
