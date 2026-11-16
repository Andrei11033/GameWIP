/// @file report_paths.cpp
/// @brief Win32 validation-runner path tests.

#include "validation/tests/runner/platform.h"

#include "test_support/test_support.h"

#include <utility>

namespace GameWIP::Test::RunnerPlatform
{
    void testWin32ReportPaths(TestSupport::Context &context, Probe runProbe, MakeOptions makeOptions, ReportOutputEnabled reportOutputEnabled)
    {
        namespace ValidationTests = GameWIP::Validation::Tests;

        ValidationTests::RunOptions rootRelative = makeOptions();
        rootRelative.writeReport = true;
        rootRelative.reportPath = R"(\escaped-report.txt)";
        GameWIP::Validation::TestResult result = runProbe(std::move(rootRelative));
        static_cast<void>(context.expectTrue("root-relative report rejection does not fail tests", result.ok()));
        static_cast<void>(context.expectFalse("root-relative report path disables report output", reportOutputEnabled()));

        ValidationTests::RunOptions driveRelative = makeOptions();
        driveRelative.writeReport = true;
        driveRelative.reportPath = "C:escaped-report.txt";
        result = runProbe(std::move(driveRelative));
        static_cast<void>(context.expectTrue("drive-relative report rejection does not fail tests", result.ok()));
        static_cast<void>(context.expectFalse("drive-relative report path disables report output", reportOutputEnabled()));
    }
} // namespace GameWIP::Test::RunnerPlatform
