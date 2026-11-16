/// @file win32_shell_launch.h
/// @brief Private Win32 shell launch-command helper.

#pragma once

#include "desktop/shell_types.h"

#include "io/status.h"

#include <string>

namespace GameWIP::Desktop::Detail::Platform
{
    /// @brief Builds a native executable and argument string from a portable launch action.
    [[nodiscard]] IO::Types::Status makeLaunchCommand(
        const Types::Shell::LaunchActionView &action,
        bool allowPlaceholders,
        std::wstring &executable,
        std::wstring &arguments);
} // namespace GameWIP::Desktop::Detail::Platform
