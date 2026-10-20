/// @file executable_path.h
/// @brief Native executable discovery shared by validation runners and child-process suites.

#pragma once

#include <filesystem>

namespace GameWIP::Validation
{
    /// @brief Returns the running executable's native absolute path independently of argv and the current directory.
    /// @throws std::system_error when the operating system cannot identify the executable.
    [[nodiscard]] std::filesystem::path currentExecutablePath();
} // namespace GameWIP::Validation
