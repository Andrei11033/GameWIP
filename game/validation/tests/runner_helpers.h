/// @file runner_helpers.h
/// @brief Shared reporting setup and finalization for correctness-module adapters.

#pragma once

#include "test_support/test_support.h"

#include <iostream>

namespace GameWIP::Test::RunnerHelpers
{
    /// @brief Maps module reporting policy into the shared TestSupport sink options.
    /// @tparam Options Module options exposing reporting and console fields.
    /// @param options Module reporting policy.
    /// @return Corresponding shared report sink options.
    template <typename Options> [[nodiscard]] inline TestSupport::Types::Reporting::Options makeReportingOptions(const Options &options)
    {
        TestSupport::Types::Reporting::Options reportOptions;
        reportOptions.writeConsole = true;
        reportOptions.consoleVerbosity =
            options.verboseConsole ? TestSupport::Types::Reporting::ConsoleVerbosity::Full : TestSupport::Types::Reporting::ConsoleVerbosity::Minimal;
        reportOptions.writeReport = options.writeReport;
        reportOptions.appendReport = options.appendReport;
        reportOptions.reportPath = options.reportPath;
        return reportOptions;
    }

    /// @brief Finalizes reporting and returns the recorded correctness result.
    /// @param runner Completed module runner.
    /// @return The runner's test exit code.
    /// @note Report sink failures are diagnosed without changing the test result.
    [[nodiscard]] inline int finalizeRunner(TestSupport::Runner &runner)
    {
        const int exitCode = runner.exitCode();
        const TestSupport::Types::Reporting::SinkStatus reportStatus = runner.finalize();

        if (!reportStatus.failureStatus.ok())
        {
            std::cerr << "[TEST INFRA] report sink failed: " << TestSupport::formatInfrastructureStatus(reportStatus.failureStatus) << '\n';
        }

        return exitCode;
    }
} // namespace GameWIP::Test::RunnerHelpers
