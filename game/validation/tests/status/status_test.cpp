/// @file status_test.cpp
/// @brief Status and Result contract checks.

#include "validation/tests/status/status_test.h"

#include "status/result.h"
#include "test_support/test_support.h"
#include "validation/tests/internal/helpers/runner_helpers.h"

#include <cstdint>
#include <format>
#include <memory>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace
{
    namespace StatusLib = GameWIP::Status;
    namespace TestSupport = GameWIP::TestSupport;

    using StatusLib::Types::Code;
    using StatusLib::Types::NativeCode;
    using StatusLib::Types::NativeCodeDomain;
    using StatusLib::Types::Result;
    using StatusLib::Types::Status;

    static_assert(!std::is_aggregate_v<Status>);
    static_assert(!std::is_default_constructible_v<Result<int>>);
    static_assert(!std::is_aggregate_v<Result<int>>);
    static_assert(std::is_same_v<decltype(std::declval<Result<int> &>().value()), int &>);
    static_assert(std::is_same_v<decltype(std::declval<const Result<int> &>().value()), const int &>);
    static_assert(std::is_same_v<decltype(std::declval<Result<int> &&>().value()), int &&>);

    // Status tests
    void testCodeNames(TestSupport::Context &context);
    void testStatusDetails(TestSupport::Context &context);

    // Result tests
    void testValueResults(TestSupport::Context &context);
    void testVoidResults(TestSupport::Context &context);

#include "validation/tests/status/status_contracts_test.inl"
} // namespace

namespace GameWIP::Test
{
    /// ------------------------------------------------------------
    /// Test runner
    /// ------------------------------------------------------------

    int runStatusTests([[maybe_unused]] int argc, [[maybe_unused]] char **argv, const StatusTestOptions &options)
    {
        const TestSupport::Types::Reporting::Options reportOptions = GameWIP::Test::Internal::RunnerHelpers::makeReportingOptions(options);
        TestSupport::Runner runner(reportOptions);

        runner.runSuite("Status code names", testCodeNames);
        runner.runSuite("Status diagnostics", testStatusDetails);
        runner.runSuite("Result values", testValueResults);
        runner.runSuite("Result<void>", testVoidResults);

        const TestSupport::Types::Reporting::Summary result = runner.result();
        runner.summary(std::format("Status checks passed={} failed={} skipped={}", result.passed, result.failed, result.skipped));
        return GameWIP::Test::Internal::RunnerHelpers::finalizeRunner(runner);
    }
} // namespace GameWIP::Test
