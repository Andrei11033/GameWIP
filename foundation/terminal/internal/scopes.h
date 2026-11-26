/// @file scopes.h
/// @brief Private operations shared by Terminal scope objects and output controls.

#pragma once

#include "terminal/output_types.h"

namespace GameWIP::Terminal::Internal
{
    /// ------------------------------------------------------------
    /// Scoped output state
    /// ------------------------------------------------------------

    [[nodiscard]] GameWIP::Status::Types::Status restoreCursorHiddenScope(
        Types::Output::Stream stream,
        const Types::Output::ControlOptions &options,
        bool &restored) noexcept;

    [[nodiscard]] GameWIP::Status::Types::Status leaveAlternateScreenScope(
        Types::Output::Stream stream,
        const Types::Output::ControlOptions &options,
        bool &restored) noexcept;

    void abandonCursorHiddenScope(Types::Output::Stream stream) noexcept;
    void abandonAlternateScreenScope(Types::Output::Stream stream) noexcept;
} // namespace GameWIP::Terminal::Internal
