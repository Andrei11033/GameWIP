/// @file platform.h
/// @brief Platform test hooks for the FileSystem suite.

#pragma once

#include <filesystem>

namespace GameWIP::TestSupport
{
    class Context;
    class Runner;
} // namespace GameWIP::TestSupport

namespace GameWIP::Test::FileSystemPlatform
{
    /// ------------------------------------------------------------
    /// Native fixtures
    /// ------------------------------------------------------------

    /// @brief Creates a test symlink and records setup failure in the current context.
    /// @param directory True when the link should target a directory.
    [[nodiscard]] bool createSymlink(
        TestSupport::Context &context,
        const std::filesystem::path &target,
        const std::filesystem::path &link,
        bool directory);

    /// ------------------------------------------------------------
    /// Native validation
    /// ------------------------------------------------------------

    /// @brief Registers platform-specific FileSystem cases under the temporary test root.
    void registerSuites(TestSupport::Runner &runner, const std::filesystem::path &root);
} // namespace GameWIP::Test::FileSystemPlatform
