/// @file win32_shell_launch.h
/// @brief Private Win32 shell launch-command helper.

#pragma once

#include "desktop/shell_types.h"

#include "status/result.h"

#include <string>

namespace GameWIP::Desktop::Platform
{
    /// @brief Builds a native executable and argument string from a portable launch action.
    [[nodiscard]] GameWIP::Status::Types::Status makeLaunchCommand(
        const Types::Shell::LaunchActionView &action,
        bool allowPlaceholders,
        std::wstring &executable,
        std::wstring &arguments);
} // namespace GameWIP::Desktop::Platform
