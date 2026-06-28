/// @file filesystem_test.h
/// @brief Runtime options and entry point for the FileSystem self-tests.

#pragma once

#include <filesystem>

namespace GameWIP::Test
{
    /// @brief Runtime toggles for the FileSystem library self-tests.
    struct FileSystemTestOptions
    {
        /// @brief Writes test progress and summaries to reportPath in addition to stdout.
        bool writeReport = true;
        /// @brief Appends to reportPath instead of replacing it.
        bool appendReport = true;
        /// @brief Text report path used when writeReport is true.
        std::filesystem::path reportPath = "logs/tests/latest_test_report.txt";
    };

    /// @brief Runs the FileSystem library self-tests.
    /// @param argc Process argument count.
    /// @param argv Process argument values.
    /// @param options Runtime report toggles.
    /// @return Zero when every FileSystem self-test passes, nonzero otherwise.
    int runFileSystemTests(int argc, char **argv, const FileSystemTestOptions &options = {});
} // namespace GameWIP::Test
