/// @file io_test.h
/// @brief Runtime options and entry point for the IO self-tests.
///
/// This header is a source-tree validation interface for the IO suite. It is not installed consumer API.

#pragma once

#include <filesystem>

namespace GameWIP::Test
{
    /// @brief Runtime toggles for the IO library self-tests.
    struct IOTestOptions
    {
        // Console output
        bool verboseConsole = false; ///< Enables full console output instead of the default minimal output.

        // Report output
        bool writeReport = true;  ///< Writes the suite report to reportPath.
        bool appendReport = true; ///< Appends to reportPath instead of replacing it when report writing is enabled.
        std::filesystem::path reportPath = "logs/validation/latest_test_report.txt"; ///< Report destination before shared-runner resolution.
    };

    /// @brief Runs the IO library self-tests.
    /// @param argc Borrowed process argument count for the duration of the call.
    /// @param argv Borrowed process argument values; pointed-to strings must remain valid for the call.
    /// @param options Runtime report toggles.
    /// @return Zero when every IO self-test passes, nonzero otherwise.
    [[nodiscard]] int runIOTests(int argc, char *const *argv, const IOTestOptions &options = {});
} // namespace GameWIP::Test
