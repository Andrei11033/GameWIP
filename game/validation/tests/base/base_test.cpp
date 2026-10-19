/// @file base_test.cpp
/// @brief Focused arithmetic and native procedure-lookup contract tests.

#include "validation/tests/base/base_test.h"

#include "base/checked_arithmetic.h"
#include "validation/tests/runner_helpers.h"
#include "test_support/test_support.h"

#if defined(_WIN32)
#include "base/platform/win32/dynamic_library.h"
#endif

#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <iostream>
#include <limits>

namespace
{
    namespace Base = GameWIP::Base;
    namespace TestSupport = GameWIP::TestSupport;

    // Narrow operands also exercise integral promotion; these checks require constant evaluation.
    static_assert(!Base::wouldAddOverflow(std::uint8_t{254}, std::uint8_t{1}));
    static_assert(Base::wouldAddOverflow(std::uint8_t{255}, std::uint8_t{1}));
    static_assert(!Base::wouldMultiplyOverflow(std::uint8_t{15}, std::uint8_t{17}));
    static_assert(Base::wouldMultiplyOverflow(std::uint8_t{16}, std::uint8_t{16}));
    static_assert(noexcept(Base::wouldAddOverflow(std::size_t{0}, std::size_t{0})));
    static_assert(noexcept(Base::wouldMultiplyOverflow(std::size_t{0}, std::size_t{0})));

    void testCheckedArithmetic(TestSupport::Context &context)
    {
        struct Case
        {
            std::size_t left;
            std::size_t right;
            bool additionOverflows;
            bool multiplicationOverflows;
        };
        constexpr std::size_t maximum = (std::numeric_limits<std::size_t>::max)();
        constexpr std::array cases{
            Case{0, 0, false, false},
            Case{0, maximum, false, false},
            Case{1, 1, false, false},
            Case{1, maximum, true, false},
            Case{maximum - 1, 1, false, false},
            Case{maximum / 4, 4, false, false},
            Case{maximum / 4 + 1, 4, false, true},
            Case{maximum, maximum, true, true},
        };
        const auto check = [&context](const Case &test, std::size_t left, std::size_t right)
        {
            static_cast<void>(context.expectEq(
                std::format("addition {} + {} {}", left, right, test.additionOverflows ? "overflows" : "fits"),
                test.additionOverflows,
                Base::wouldAddOverflow(left, right)));
            static_cast<void>(context.expectEq(
                std::format("multiplication {} * {} {}", left, right, test.multiplicationOverflows ? "overflows" : "fits"),
                test.multiplicationOverflows,
                Base::wouldMultiplyOverflow(left, right)));
        };
        for (const Case &test : cases)
        {
            check(test, test.left, test.right);
            if (test.left != test.right)
            {
                check(test, test.right, test.left);
            }
        }
    }

#if defined(_WIN32)
    void testProcedureLookup(TestSupport::Context &context)
    {
        using ProcessIdFunction = decltype(&GetCurrentProcessId);
        // Borrow the existing system module; the lookup helper does not own its lifetime.
        const HMODULE module = GetModuleHandleW(L"kernel32.dll");
        if (!context.expectTrue("system module is already loaded", module != nullptr))
        {
            return;
        }
        static_cast<void>(context.expectTrue(
            "null module returns no procedure",
            Base::Win32::loadProcedure<ProcessIdFunction>(nullptr, "GetCurrentProcessId") == nullptr));
        static_cast<void>(
            context.expectTrue("null export name returns no procedure", Base::Win32::loadProcedure<ProcessIdFunction>(module, nullptr) == nullptr));
        static_cast<void>(context.expectTrue(
            "missing export returns no procedure",
            Base::Win32::loadProcedure<ProcessIdFunction>(module, "GameWIP_Base_MissingExport") == nullptr));
        const ProcessIdFunction processId = Base::Win32::loadProcedure<ProcessIdFunction>(module, "GetCurrentProcessId");
        static_cast<void>(context.expectTrue("known export resolves", processId != nullptr));
        if (processId != nullptr)
        {
            static_cast<void>(context.expectEq("resolved procedure returns this process id", GetCurrentProcessId(), processId()));
        }
    }
#endif
} // namespace

namespace GameWIP::Test
{
    int runBaseTests(const BaseTestOptions &options)
    {
        TestSupport::Types::Reporting::Options reportOptions = GameWIP::Test::RunnerHelpers::makeReportingOptions(options);

        TestSupport::Runner runner(reportOptions);
        runner.runSuite("Base checked arithmetic", testCheckedArithmetic);
#if defined(_WIN32)
        runner.runSuite("Base Win32 procedure lookup", testProcedureLookup);
#endif

        return GameWIP::Test::RunnerHelpers::finalizeRunner(runner);
    }
} // namespace GameWIP::Test
