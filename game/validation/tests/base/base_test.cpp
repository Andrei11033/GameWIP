/// @file base_test.cpp
/// @brief Boundary tests for internal checked-arithmetic mechanisms.

#include "validation/tests/base/base_test.h"

#include "base/checked_arithmetic.h"
#include "test_support/test_support.h"

#include <cstddef>
#include <cstdint>
#include <limits>

namespace GameWIP::Test
{
    int runBaseTests(const BaseTestOptions &options)
    {
        namespace Base = GameWIP::Base;
        namespace TestSupport = GameWIP::TestSupport;

        TestSupport::Types::Reporting::Options reportOptions;
        reportOptions.writeConsole = true;
        reportOptions.consoleVerbosity =
            options.verboseConsole ? TestSupport::Types::Reporting::ConsoleVerbosity::Full : TestSupport::Types::Reporting::ConsoleVerbosity::Minimal;
        reportOptions.writeReport = options.writeReport;
        reportOptions.appendReport = options.appendReport;
        reportOptions.reportPath = options.reportPath;

        TestSupport::Runner runner(reportOptions);
        runner.runSuite(
            "Base checked arithmetic",
            [](TestSupport::Context &context)
            {
                constexpr std::size_t maximum = (std::numeric_limits<std::size_t>::max)();
                static_cast<void>(context.expectFalse("zero plus zero", Base::wouldAddOverflow(std::size_t{0}, std::size_t{0})));
                static_cast<void>(context.expectFalse("one plus one", Base::wouldAddOverflow(std::size_t{1}, std::size_t{1})));
                static_cast<void>(context.expectFalse("maximum plus zero", Base::wouldAddOverflow(maximum, std::size_t{0})));
                static_cast<void>(context.expectFalse("safe addition boundary", Base::wouldAddOverflow(maximum - 1, std::size_t{1})));
                static_cast<void>(context.expectTrue("addition overflow boundary", Base::wouldAddOverflow(maximum, std::size_t{1})));
                static_cast<void>(context.expectFalse("zero multiplication", Base::wouldMultiplyOverflow(std::size_t{0}, maximum)));
                static_cast<void>(context.expectFalse("one multiplication", Base::wouldMultiplyOverflow(std::size_t{1}, maximum)));
                static_cast<void>(context.expectFalse("safe multiplication boundary", Base::wouldMultiplyOverflow(maximum / 4, std::size_t{4})));
                static_cast<void>(
                    context.expectTrue("multiplication overflow boundary", Base::wouldMultiplyOverflow(maximum / 4 + 1, std::size_t{4})));

                constexpr std::size_t iconWidth = 4096;
                constexpr std::size_t iconHeight = 4096;
                constexpr std::size_t channels = 4;
                static_cast<void>(context.expectFalse("representative pixel count", Base::wouldMultiplyOverflow(iconWidth, iconHeight)));
                static_cast<void>(context.expectFalse("representative RGBA storage", Base::wouldMultiplyOverflow(iconWidth * iconHeight, channels)));
                static_cast<void>(
                    context.expectTrue("representative queue arena overflow", Base::wouldMultiplyOverflow(maximum / 256 + 1, std::size_t{256})));
            });

        return runner.exitCode();
    }
} // namespace GameWIP::Test
