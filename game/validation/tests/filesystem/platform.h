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

    bool createSymlink(TestSupport::Context &context, const std::filesystem::path &target, const std::filesystem::path &link, bool directory);

    /// ------------------------------------------------------------
    /// Native validation
    /// ------------------------------------------------------------

    void registerSuites(TestSupport::Runner &runner, const std::filesystem::path &root);
} // namespace GameWIP::Test::FileSystemPlatform
