/// @file display_report.h
/// @brief Startup display inspection and diagnostic reporting for the executable runtime.

#pragma once

#include "desktop/display_info.h"

#include <vector>

namespace GameWIP::Game
{
    /// @brief Enumerates connected displays and reports their inventory and capabilities.
    /// @note Debug reports each display; Trace adds measurements and supported modes. Query failures are Warn records.
    /// @return The enumerated displays, or the enumeration failure for the caller to report.
    [[nodiscard]] Desktop::Types::Result<std::vector<Desktop::Types::Display::Info>> inspectDisplays();
} // namespace GameWIP::Game
