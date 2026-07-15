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
        /// @brief Mirrors complete suite output to stdout instead of only failures, skips, and manual instructions.
        bool verboseConsole = false;
        /// @brief Writes test progress and summaries to reportPath in addition to stdout.
        bool writeReport = true;
        /// @brief Appends to reportPath instead of replacing it when report writing is enabled.
        bool appendReport = true;
        /// @brief Report destination used as supplied; the shared runner normally resolves it before invocation.
        std::filesystem::path reportPath = "logs/tests/latest_test_report.txt";
    };

    /// @brief Runs the FileSystem library self-tests.
    /// @param argc Borrowed process argument count for the duration of the call.
    /// @param argv Borrowed process argument values; pointed-to strings must remain valid for the call.
    /// @param options Runtime report toggles.
    /// @return Zero when every FileSystem self-test passes, nonzero otherwise.
    int runFileSystemTests(int argc, char **argv, const FileSystemTestOptions &options = {});
} // namespace GameWIP::Test
