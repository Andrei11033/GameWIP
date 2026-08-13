/// @file terminal_input.h
/// @brief Internal managed-input ownership and serialization boundary for Terminal.

#pragma once

#include "terminal/terminal.h"

#include <cstddef>
#include <mutex>
#include <vector>

namespace GameWIP::Terminal::Detail
{

    /// @brief Runs Terminal-owned grapheme-aware line editing through the active managed input owner.
    [[nodiscard]] Types::Input::LineResult managedTerminalLineRead(
        Types::Input::Stream input,
        Types::Output::Stream output,
        const Types::Input::LineOptions &options,
        std::vector<std::size_t> &graphemeStorage);

    /// @brief Returns the process-wide mutex serializing backend input access for one standard input stream.
    [[nodiscard]] std::mutex &inputIoMutex(Types::Input::Stream stream) noexcept;

    /// @brief Claims exclusive managed ownership of one standard input stream for owner.
    /// @return Success or ResourceBusy when another Session/direct operation already owns the stream.
    [[nodiscard]] IO::Types::Status claimInput(Types::Input::Stream stream, const void *owner) noexcept;

    /// @brief Releases managed input ownership when owner is the current claimant.
    void releaseInput(Types::Input::Stream stream, const void *owner) noexcept;

    /// @brief Queries the backend-stable cursor coordinate used by managed line rendering.
    /// @details Unlike the public viewport-relative query, this coordinate remains stable when the visible Win32
    /// console window scrolls. The call shares the normal input/output serialization domain.
    [[nodiscard]] Types::Cursor::PositionResult getLineRenderingCursorPosition(
        Types::Output::Stream outputStream,
        Types::Input::Stream inputStream) noexcept;

    /// @brief Positions the cursor using the backend-stable coordinate used by managed line rendering.
    /// @details The call shares the normal output serialization domain.
    [[nodiscard]] IO::Types::Status setLineRenderingCursorPosition(Types::Output::Stream outputStream, Types::Cursor::Position position) noexcept;
} // namespace GameWIP::Terminal::Detail
