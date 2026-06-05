/// @file terminal_platform.h
/// @brief Internal platform abstraction used by the Terminal library.

#pragma once

#include "terminal/terminal.h"

#include <span>
#include <string_view>

namespace GameWIP::Terminal::Detail::Platform
{
    /// @brief Returns the platform-native line ending used by Types::LineEnding::Native.
    /// @return Non-owning string literal for the current backend.
    [[nodiscard]] std::string_view nativeLineEnding() noexcept;

    /// @brief Gets input capabilities for a standard terminal input stream.
    /// @param stream Standard input stream to inspect.
    /// @return Portable capability result. Detached streams return success with detached capabilities.
    [[nodiscard]] Terminal::Types::InputCapabilityResult getInputCapabilities(Terminal::Types::InputStream stream);

    /// @brief Gets output capabilities for a standard terminal output stream.
    /// @param stream Standard output stream to inspect.
    /// @return Portable capability result. Detached streams return success with detached capabilities.
    [[nodiscard]] Terminal::Types::OutputCapabilityResult getOutputCapabilities(Terminal::Types::OutputStream stream);

    /// @brief Enables platform output features needed by styling and terminal controls.
    /// @return Capabilities active after the idempotent preparation attempt.
    [[nodiscard]] Terminal::Types::OutputCapabilityResult prepareOutput(Terminal::Types::OutputStream stream);

    /// @brief Checks whether input can be read without a normal blocking wait.
    /// @param stream Standard input stream to inspect.
    /// @return Best-effort availability result.
    [[nodiscard]] Terminal::Types::InputAvailabilityResult getInputAvailability(Terminal::Types::InputStream stream);

    /// @brief Gets the current input mode for a standard input stream.
    /// @param stream Standard input stream to inspect.
    /// @return Portable input mode or an Unsupported/StatFailed-style status.
    [[nodiscard]] Terminal::Types::InputModeResult getInputMode(Terminal::Types::InputStream stream);

    /// @brief Sets the current input mode for a standard input stream.
    /// @param stream Standard input stream to update.
    /// @param mode Portable input mode request.
    /// @return Success or portable/backend-native failure status.
    [[nodiscard]] IO::Types::Status setInputMode(Terminal::Types::InputStream stream, const Terminal::Types::InputMode &mode);

    /// @brief Restores the backend/default input mode for a standard input stream.
    /// @param stream Standard input stream to restore.
    /// @return Success or portable/backend-native failure status.
    [[nodiscard]] IO::Types::Status restoreDefaultInputMode(Terminal::Types::InputStream stream);

    /// @brief Gets terminal size for a standard output stream when the backend can query it.
    /// @param stream Standard output stream to inspect.
    /// @return Terminal size or an Unsupported/StatFailed-style status.
    [[nodiscard]] Terminal::Types::TerminalSizeResult getTerminalSize(Terminal::Types::OutputStream stream);

    /// @brief Gets cursor position for a standard output stream when the backend can query it reliably.
    /// @param stream Standard output stream to inspect.
    /// @return Cursor position or an Unsupported/StatFailed-style status.
    [[nodiscard]] Terminal::Types::CursorPositionResult getCursorPosition(Terminal::Types::OutputStream stream);

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
} // namespace GameWIP::Terminal::Detail::Platform
