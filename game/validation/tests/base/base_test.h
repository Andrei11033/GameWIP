/// @file base_test.h
/// @brief Source-tree entry point for internal Base mechanism tests.

#pragma once

#include <filesystem>

namespace GameWIP::Test
{
    /// @brief Runtime reporting options for internal Base mechanism tests.
    struct BaseTestOptions
    {
        /// @brief Mirrors complete suite output to stdout instead of only failures, skips, and manual instructions.
        bool verboseConsole = false;
        /// @brief Writes test progress and summaries to reportPath in addition to stdout.
        bool writeReport = true;
        /// @brief Appends to reportPath instead of replacing it when report writing is enabled.
        bool appendReport = true;
        /// @brief Report destination used as supplied; the shared runner normally resolves it before invocation.
        std::filesystem::path reportPath = "logs/validation/latest_test_report.txt";
    };

    /// @brief Runs internal Base mechanism tests.
    /// @param options Runtime report toggles.
    int runBaseTests(const BaseTestOptions &options = {});
} // namespace GameWIP::Test
