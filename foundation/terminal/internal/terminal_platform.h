/// @file terminal_platform.h
/// @brief Internal backend boundary used by the platform-neutral Terminal core.
/// @details Native handles, encoding conversion, and endpoint-specific behavior stay behind this interface.

#pragma once

#include "terminal/terminal.h"

#include <cstdint>
#include <span>
#include <string_view>

#if TERMINAL_INTERNAL_TEST_HOOKS && defined(_WIN32)
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

    /// @brief Returns the platform-native line ending used by Types::Output::LineEnding::Native.
    /// @return Non-owning string literal for the current backend.
    [[nodiscard]] std::string_view nativeLineEnding() noexcept;

    // ------------------------------------------------------------
    // Capabilities and endpoint state
    // ------------------------------------------------------------

    /// @brief Returns input capabilities for a standard terminal input stream.
    /// @param stream Standard input stream to inspect.
    /// @return Portable capability result. Detached streams return success with detached capabilities.
    [[nodiscard]] Terminal::Types::Input::CapabilitiesResult getInputCapabilities(Terminal::Types::Input::Stream stream);

    /// @brief Returns output capabilities for a standard terminal output stream.
    /// @param stream Standard output stream to inspect.
    /// @return Portable capability result. Detached streams return success with detached capabilities.
    [[nodiscard]] Terminal::Types::Output::CapabilitiesResult getOutputCapabilities(Terminal::Types::Output::Stream stream);

    /// @brief Enables platform output features needed by styling and terminal controls.
    /// @return Capabilities active after the idempotent preparation attempt.
    [[nodiscard]] Terminal::Types::Output::CapabilitiesResult prepareOutput(Terminal::Types::Output::Stream stream);

    /// @brief Validates a cursor movement amount for the selected backend.
    [[nodiscard]] IO::Types::Status validateCursorMovement(Terminal::Types::Output::Stream stream, std::uint32_t amount);

    /// @brief Validates a cursor position for the selected backend.
    [[nodiscard]] IO::Types::Status validateCursorPosition(Terminal::Types::Output::Stream stream, Terminal::Types::Cursor::Position position);

    /// @brief Validates a scroll amount for the selected backend.
    [[nodiscard]] IO::Types::Status validateScroll(Terminal::Types::Output::Stream stream, std::uint32_t lines);

    /// @brief Validates a terminal title for the selected backend.
    [[nodiscard]] IO::Types::Status validateTitle(Terminal::Types::Output::Stream stream, std::string_view utf8Title);

    /// @brief Captures the current portable and backend-native input mode.
    /// @param stream Standard input stream to inspect.
    /// @return Complete snapshot suitable for exact managed restoration.
    [[nodiscard]] InputModeSnapshotResult captureInputMode(Terminal::Types::Input::Stream stream);

    /// @brief Sets an internal managed input mode for a standard input stream.
    /// @param stream Standard input stream to update.
    /// @param mode Internal mode request.
    /// @return Success or portable/backend-native failure status.
    [[nodiscard]] IO::Types::Status setInputMode(Terminal::Types::Input::Stream stream, const InputMode &mode);

    /// @brief Restores a previously captured complete input mode.
    /// @param stream Standard input stream to restore.
    /// @param snapshot Snapshot produced by captureInputMode().
    /// @return Success or portable/backend-native failure status.
    [[nodiscard]] IO::Types::Status restoreInputMode(Terminal::Types::Input::Stream stream, const InputModeSnapshot &snapshot);

    // ------------------------------------------------------------
    // Geometry and cursor state
    // ------------------------------------------------------------

    /// @brief Returns the terminal size for a standard output stream when the backend can query it.
    /// @param stream Standard output stream to inspect.
    /// @return Terminal size or an Unsupported/StatFailed-style status.
    [[nodiscard]] Terminal::Types::SizeResult getTerminalSize(Terminal::Types::Output::Stream stream);

    /// @brief Returns the cursor position for a standard output stream when the backend can query it reliably.
    /// @param outputStream Standard output stream to inspect or use for a protocol query.
    /// @param responseStream Standard input stream used for a protocol response when required.
    /// @param options Query timeout and flush behavior. The core applies flush behavior before this call.
    /// @return Cursor position or an Unsupported/StatFailed-style status.
    [[nodiscard]] Terminal::Types::Cursor::PositionResult getCursorPosition(
        Terminal::Types::Output::Stream outputStream,
        Terminal::Types::Input::Stream responseStream,
        const Terminal::Types::Cursor::QueryOptions &options);

    /// @brief Returns a backend-stable cursor coordinate for managed line rendering.
    [[nodiscard]] Terminal::Types::Cursor::PositionResult getLineRenderingCursorPosition(Terminal::Types::Output::Stream stream);

    /// @brief Sets a backend-stable cursor coordinate for managed line rendering.
    [[nodiscard]] IO::Types::Status setLineRenderingCursorPosition(
        Terminal::Types::Output::Stream stream,
        Terminal::Types::Cursor::Position position);

    /// @brief Reads one normalized structured event from a standard input stream.
    /// @param stream Standard input stream to read.
    /// @param outputStream Bound output used to resolve resize dimensions consistently with getTerminalSize().
    /// @param options Event-read deadline and cancellation behavior.
    /// @return Event result, normal stopping outcome, or checked backend failure.
    [[nodiscard]] Terminal::Types::Input::EventResult readEvent(
        Terminal::Types::Input::Stream stream,
        Terminal::Types::Output::Stream outputStream,
        const Terminal::Types::Input::EventOptions &options);

    /// @brief Reads bytes from a standard input stream.
    /// @param stream Standard input stream to read.
    /// @param outputBuffer Caller-owned output storage.
    /// @param options Byte-read behavior.
    /// @return Terminal byte read result.
    [[nodiscard]] Terminal::Types::Input::ByteResult readBytes(
        Terminal::Types::Input::Stream stream,
        std::span<std::byte> outputBuffer,
        const Terminal::Types::Input::ByteOptions &options);

    /// @brief Reads one available UTF-8 text chunk from a standard input stream.
    /// @param stream Standard input stream to read.
    /// @param options Text-read behavior.
    /// @return Terminal text read result.
    [[nodiscard]] Terminal::Types::Input::TextResult readText(
        Terminal::Types::Input::Stream stream,
        const Terminal::Types::Input::TextOptions &options);

    /// @brief Reads one UTF-8 line from a standard input stream.
    /// @param stream Standard input stream to read.
    /// @param options Line-read behavior.
    /// @return Terminal line read result.
    [[nodiscard]] Terminal::Types::Input::LineResult readLine(
        Terminal::Types::Input::Stream stream,
        const Terminal::Types::Input::LineOptions &options);

    /// @brief Writes UTF-8 text to a standard output stream.
    /// @details The caller supplies complete valid UTF-8. Windows real consoles convert it to UTF-16 and use WriteConsoleW; redirected text writes
    /// preserve the validated UTF-8 bytes.
    /// @param stream Standard output stream to write.
    /// @param utf8Text Already-validated complete UTF-8 text.
    /// @return Success or portable/backend-native failure status.
    [[nodiscard]] IO::Types::Status writeText(Terminal::Types::Output::Stream stream, std::string_view utf8Text);

    /// @brief Writes bytes to a standard output stream where practical.
    /// @param stream Standard output stream to write.
    /// @param bytes Caller-owned arbitrary binary bytes; no Unicode validity is required.
    /// @return Number of accepted bytes and operation status.
    [[nodiscard]] IO::Types::WriteResult writeBytes(Terminal::Types::Output::Stream stream, std::span<const std::byte> bytes);

    /// @brief Flushes a standard output stream where meaningful.
    /// @param stream Standard output stream to flush.
    /// @param mode Requested flush strength.
    /// @return Success or portable/backend-native failure status.
    [[nodiscard]] IO::Types::Status flush(Terminal::Types::Output::Stream stream, IO::Types::FlushMode mode);

#if TERMINAL_INTERNAL_TEST_HOOKS
    // ------------------------------------------------------------
    // Test hooks
    // ------------------------------------------------------------

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
        void setPendingHighSurrogate(Terminal::Types::Input::Stream stream, std::uint16_t surrogate) noexcept;
        /// @brief Returns whether the current Win32 input endpoint retains a pending high surrogate.
        [[nodiscard]] bool hasPendingHighSurrogate(Terminal::Types::Input::Stream stream) noexcept;
    } // namespace TestHooks
#endif
} // namespace GameWIP::Terminal::Detail::Platform
