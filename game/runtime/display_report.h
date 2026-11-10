/// @file display_report.h
/// @brief Startup display inspection and diagnostic reporting for the executable runtime.

#pragma once

#include "desktop/display_info.h"

#include <vector>

namespace GameWIP::Game
{
    /// @brief Enumerates connected displays and logs their active mode, color capabilities, and supported modes.
    /// @note Per-display summaries and the full supported-mode list are logged at Debug.
    /// @return The enumerated displays, or the enumeration failure for the caller to report.
    [[nodiscard]] Desktop::Types::Result<std::vector<Desktop::Types::Display::Info>> inspectDisplays();
} // namespace GameWIP::Game
