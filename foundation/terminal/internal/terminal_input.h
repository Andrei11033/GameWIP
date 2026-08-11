/// @file terminal_input.h
/// @brief Internal managed-input ownership and serialization boundary for Terminal.

#pragma once

#include "terminal/terminal.h"

#include <mutex>

namespace GameWIP::Terminal::Detail
{
    /// @brief Returns the process-wide mutex serializing backend input access for one standard input stream.
    [[nodiscard]] std::mutex &inputIoMutex(Types::InputStream stream) noexcept;

    /// @brief Claims exclusive managed ownership of one standard input stream for owner.
    /// @return Success or ResourceBusy when another Session/direct operation already owns the stream.
    [[nodiscard]] IO::Types::Status claimInput(Types::InputStream stream, const void *owner) noexcept;

    /// @brief Releases managed input ownership when owner is the current claimant.
    void releaseInput(Types::InputStream stream, const void *owner) noexcept;

    /// @brief Queries the backend-stable cursor coordinate used by managed line rendering.
    /// @details Unlike the public viewport-relative query, this coordinate remains stable when the visible Win32
    /// console window scrolls. The call shares the normal input/output serialization domain.
    [[nodiscard]] Types::CursorPositionResult getLineRenderingCursorPosition(
        Types::OutputStream outputStream,
        Types::InputStream inputStream) noexcept;

    /// @brief Positions the cursor using the backend-stable coordinate used by managed line rendering.
    /// @details The call shares the normal output serialization domain.
    [[nodiscard]] IO::Types::Status setLineRenderingCursorPosition(Types::OutputStream outputStream, Types::CursorPosition position) noexcept;
} // namespace GameWIP::Terminal::Detail
