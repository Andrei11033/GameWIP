/// @file status_test.h
/// @brief Runtime options and entry point for Status correctness checks.
///
/// This header is a source-tree validation interface and is not installed.

#pragma once

#include <filesystem>

namespace GameWIP::Test
{
    /// @brief Reporting options for the Status checks.
    struct StatusTestOptions
    {
        // Report output
        bool verboseConsole = false;                                                 ///< Enables full console output.
        bool writeReport = true;                                                     ///< Writes the suite report.
        bool appendReport = false;                                                   ///< Appends instead of replacing the report.
        std::filesystem::path reportPath = "logs/validation/status_test_report.txt"; ///< Report destination.
    };

    /// @brief Runs Status and Result contract checks.
    /// @param argc Original process argument count.
    /// @param argv Borrowed original process arguments.
    /// @param options Report output settings.
    /// @return Zero when every check passes, nonzero otherwise.
    [[nodiscard]] int runStatusTests(int argc, char **argv, const StatusTestOptions &options);
} // namespace GameWIP::Test
