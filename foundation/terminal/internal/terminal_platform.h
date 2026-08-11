/// @file terminal_platform.h
/// @brief Internal backend boundary used by the platform-neutral Terminal core.
/// @details Native handles, encoding conversion, and endpoint-specific behavior stay behind this interface.

#pragma once

#include "terminal/terminal.h"

#include <cstdint>
#include <span>
#include <string_view>

#if INTERNAL_TERMINAL_TEST_HOOKS && defined(_WIN32)
namespace GameWIP::Terminal::TestHooks
{
    struct Win32KeyDecodeResult;
}
#endif

namespace GameWIP::Terminal::Detail::Platform
{
    /// @brief Internal portable shape used only to configure and restore backend terminal input state.
    struct InputMode
    {
        bool lineBuffered = true;
        bool echoInput = true;
        bool processControlKeys = true;
        bool reportResizeEvents = false;
        bool reportPointerEvents = false;
        bool exclusiveEventDelivery = false;
    };

    /// @brief Complete backend input-mode snapshot used by managed Session/direct-read restoration.
    struct InputModeSnapshot
    {
        InputMode mode{};
        std::uint64_t nativeMode = 0;
        bool hasNativeMode = false;
    };

    /// @brief Result returned when capturing the complete backend input mode.
    struct InputModeSnapshotResult
    {
        IO::Types::Status status;
        InputModeSnapshot snapshot{};
    };

    /// @brief Returns the platform-native line ending used by Types::LineEnding::Native.
    /// @return Non-owning string literal for the current backend.
    [[nodiscard]] std::string_view nativeLineEnding() noexcept;

    /// @brief Gets input capabilities for a standard terminal input stream.
    /// @param stream Standard input stream to inspect.
    /// @return Portable capability result. Detached streams return success with detached capabilities.
    [[nodiscard]] Terminal::Types::InputCapabilitiesResult getInputCapabilities(Terminal::Types::InputStream stream);

    /// @brief Gets output capabilities for a standard terminal output stream.
    /// @param stream Standard output stream to inspect.
    /// @return Portable capability result. Detached streams return success with detached capabilities.
    [[nodiscard]] Terminal::Types::OutputCapabilitiesResult getOutputCapabilities(Terminal::Types::OutputStream stream);

    /// @brief Enables platform output features needed by styling and terminal controls.
    /// @return Capabilities active after the idempotent preparation attempt.
    [[nodiscard]] Terminal::Types::OutputCapabilitiesResult prepareOutput(Terminal::Types::OutputStream stream);

    /// @brief Validates a cursor movement amount for the selected backend.
    [[nodiscard]] IO::Types::Status validateCursorMovement(Terminal::Types::OutputStream stream, std::uint32_t amount);

    /// @brief Validates a cursor position for the selected backend.
    [[nodiscard]] IO::Types::Status validateCursorPosition(Terminal::Types::OutputStream stream, Terminal::Types::CursorPosition position);

    /// @brief Validates a scroll amount for the selected backend.
    [[nodiscard]] IO::Types::Status validateScroll(Terminal::Types::OutputStream stream, std::uint32_t lines);

    /// @brief Validates a terminal title for the selected backend.
    [[nodiscard]] IO::Types::Status validateTitle(Terminal::Types::OutputStream stream, std::string_view utf8Title);

    /// @brief Captures the current portable and backend-native input mode.
    /// @param stream Standard input stream to inspect.
    /// @return Complete snapshot suitable for exact managed restoration.
    [[nodiscard]] InputModeSnapshotResult captureInputMode(Terminal::Types::InputStream stream);

    /// @brief Sets an internal managed input mode for a standard input stream.
    /// @param stream Standard input stream to update.
    /// @param mode Internal mode request.
    /// @return Success or portable/backend-native failure status.
    [[nodiscard]] IO::Types::Status setInputMode(Terminal::Types::InputStream stream, const InputMode &mode);

    /// @brief Restores a previously captured complete input mode.
    /// @param stream Standard input stream to restore.
    /// @param snapshot Snapshot produced by captureInputMode().
    /// @return Success or portable/backend-native failure status.
    [[nodiscard]] IO::Types::Status restoreInputMode(Terminal::Types::InputStream stream, const InputModeSnapshot &snapshot);

    /// @brief Gets terminal size for a standard output stream when the backend can query it.
    /// @param stream Standard output stream to inspect.
    /// @return Terminal size or an Unsupported/StatFailed-style status.
    [[nodiscard]] Terminal::Types::TerminalSizeResult getTerminalSize(Terminal::Types::OutputStream stream);

    /// @brief Gets cursor position for a standard output stream when the backend can query it reliably.
    /// @param outputStream Standard output stream to inspect or use for a protocol query.
    /// @param responseStream Standard input stream used for a protocol response when required.
    /// @param options Query timeout and flush behavior. The core applies flush behavior before this call.
    /// @return Cursor position or an Unsupported/StatFailed-style status.
    [[nodiscard]] Terminal::Types::CursorPositionResult getCursorPosition(
        Terminal::Types::OutputStream outputStream,
        Terminal::Types::InputStream responseStream,
        const Terminal::Types::CursorPositionQueryOptions &options);

    /// @brief Reads one normalized structured event from a standard input stream.
    /// @param stream Standard input stream to read.
    /// @param outputStream Bound output used to resolve resize dimensions consistently with getTerminalSize().
    /// @param options Event-read deadline and cancellation behavior.
    /// @return Event result, normal stopping outcome, or checked backend failure.
    [[nodiscard]] Terminal::Types::EventReadResult readEvent(
        Terminal::Types::InputStream stream,
        Terminal::Types::OutputStream outputStream,
        const Terminal::Types::EventReadOptions &options);

    /// @brief Reads bytes from a standard input stream.
    /// @param stream Standard input stream to read.
    /// @param outputBuffer Caller-owned output storage.
    /// @param options Byte-read behavior.
    /// @return Terminal byte read result.
    [[nodiscard]] Terminal::Types::ByteReadResult readBytes(
        Terminal::Types::InputStream stream,
        std::span<std::byte> outputBuffer,
        const Terminal::Types::ByteReadOptions &options);

    /// @brief Reads one available UTF-8 text chunk from a standard input stream.
    /// @param stream Standard input stream to read.
    /// @param options Text-read behavior.
    /// @return Terminal text read result.
    [[nodiscard]] Terminal::Types::TextReadResult readText(Terminal::Types::InputStream stream, const Terminal::Types::TextReadOptions &options);

    /// @brief Reads one UTF-8 line from a standard input stream.
    /// @param stream Standard input stream to read.
    /// @param options Line-read behavior.
    /// @return Terminal line read result.
    [[nodiscard]] Terminal::Types::LineReadResult readLine(Terminal::Types::InputStream stream, const Terminal::Types::LineReadOptions &options);

    /// @brief Writes UTF-8 text to a standard output stream.
    /// @details Windows real consoles convert UTF-8 to UTF-16 and use WriteConsoleW. Redirected output remains bytes.
    /// @param stream Standard output stream to write.
    /// @param utf8Text Public UTF-8 text.
    /// @return Success or portable/backend-native failure status.
    [[nodiscard]] IO::Types::Status writeText(Terminal::Types::OutputStream stream, std::string_view utf8Text);

    /// @brief Writes bytes to a standard output stream where practical.
    /// @param stream Standard output stream to write.
    /// @param bytes Caller-owned bytes.
    /// @return Number of accepted bytes and operation status.
    [[nodiscard]] IO::Types::WriteResult writeBytes(Terminal::Types::OutputStream stream, std::span<const std::byte> bytes);

    /// @brief Flushes a standard output stream where meaningful.
    /// @param stream Standard output stream to flush.
    /// @param mode Requested flush strength.
    /// @return Success or portable/backend-native failure status.
    [[nodiscard]] IO::Types::Status flush(Terminal::Types::OutputStream stream, IO::Types::FlushMode mode);

#if INTERNAL_TERMINAL_TEST_HOOKS
    namespace TestHooks
    {
#if defined(_WIN32)
        void resetWin32KeyDecoder() noexcept;
        [[nodiscard]] Terminal::TestHooks::Win32KeyDecodeResult decodeWin32KeyRecord(
            bool keyDown,
            std::uint16_t virtualKey,
            char16_t unicodeCharacter,
            std::uint32_t controlState,
            std::uint16_t repeatCount,
            std::uint16_t scanCode) noexcept;
        [[nodiscard]] std::optional<Terminal::Types::Event> takePendingWin32KeyEvent() noexcept;
#endif

        /// @brief Seeds the Win32 pending UTF-16 high surrogate for endpoint-replacement validation.
        void setPendingHighSurrogate(Terminal::Types::InputStream stream, std::uint16_t surrogate) noexcept;
        /// @brief Returns whether the current Win32 input endpoint retains a pending high surrogate.
        [[nodiscard]] bool hasPendingHighSurrogate(Terminal::Types::InputStream stream) noexcept;
    } // namespace TestHooks
#endif
} // namespace GameWIP::Terminal::Detail::Platform
