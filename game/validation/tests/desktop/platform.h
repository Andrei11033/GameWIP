/// @file platform.h
/// @brief Platform test hooks for the Desktop suite.

#pragma once

namespace GameWIP::Test::DesktopPlatform
{
    /// ------------------------------------------------------------
    /// Child process routing
    /// ------------------------------------------------------------

    /// @brief Returns whether the arguments belong to a Desktop native child protocol.
    [[nodiscard]] bool handlesChildArguments(int argc, char **argv);
} // namespace GameWIP::Test::DesktopPlatform
