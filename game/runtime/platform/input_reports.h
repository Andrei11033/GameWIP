/// @file input_reports.h
/// @brief Platform contract for native Input report details.

#pragma once

#include "input/frame.h"

#include <cstdint>

namespace GameWIP::Game::Runtime::Platform
{
    /// @brief Logs native details that can be decoded from one retained report.
    void logRawInputDetails(const GameWIP::Input::Types::RawReport &report, std::uint64_t captureSequence);
} // namespace GameWIP::Game::Runtime::Platform
