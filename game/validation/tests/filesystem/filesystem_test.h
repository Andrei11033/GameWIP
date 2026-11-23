/// @file filesystem_test.h
/// @brief Runtime options and entry point for the FileSystem self-tests.
///
/// This header is a source-tree validation interface for the FileSystem suite. It is not installed consumer API.

#pragma once

#include <filesystem>

namespace GameWIP::Test
{
    /// @brief Runtime toggles for the FileSystem library self-tests.
    struct FileSystemTestOptions
    {
        // Console output
        bool verboseConsole = false; ///< Enables full console output instead of the default minimal output.

        // Report output
        bool writeReport = true;  ///< Writes the suite report to reportPath.
        bool appendReport = true; ///< Appends to reportPath instead of replacing it when report writing is enabled.
        std::filesystem::path reportPath = "logs/validation/latest_test_report.txt"; ///< Report destination before shared-runner resolution.
    };

    /// @brief Runs the FileSystem library self-tests.
    /// @param argc Borrowed process argument count for the duration of the call.
    /// @param argv Borrowed process argument values; pointed-to strings must remain valid for the call.
    /// @param options Runtime report toggles.
    /// @return Zero when every FileSystem self-test passes, nonzero otherwise.
    [[nodiscard]] int runFileSystemTests(int argc, char **argv, const FileSystemTestOptions &options = {});
} // namespace GameWIP::Test
