#pragma once

#include "test_support/test_support.h"

#include <iostream>

namespace GameWIP::Test::RunnerHelpers
{
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
