/// @file base.cpp
/// @brief Win32 procedure-lookup validation.

#include "validation/tests/base/platform.h"

#include "base/platform/win32/dynamic_library.h"
#include "test_support/test_support.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace
{
    namespace Base = GameWIP::Base;
    namespace TestSupport = GameWIP::TestSupport;

    /// ------------------------------------------------------------
    /// Procedure lookup
    /// ------------------------------------------------------------

    /// @brief Verifies Win32 procedure lookup with borrowed and invalid module inputs.
    void testProcedureLookup(TestSupport::Context &context)
    {
        using ProcessIdFunction = decltype(&GetCurrentProcessId);

        // Borrow the existing system module; the lookup helper does not own its lifetime.
        const HMODULE module = GetModuleHandleW(L"kernel32.dll");

        if(!context.expectTrue("system module is already loaded", module != nullptr))
        {
            return;
        }

        static_cast<void>(context.expectTrue(
            "null module returns no procedure",
            Base::Platform::Win32::loadProcedure<ProcessIdFunction>(nullptr, "GetCurrentProcessId") == nullptr));

        static_cast<void>(context.expectTrue(
            "null export name returns no procedure",
            Base::Platform::Win32::loadProcedure<ProcessIdFunction>(module, nullptr) == nullptr));

        static_cast<void>(context.expectTrue(
            "missing export returns no procedure",
            Base::Platform::Win32::loadProcedure<ProcessIdFunction>(module, "GameWIP_Base_MissingExport") == nullptr));

        // Resolve and invoke a known system export.
        const ProcessIdFunction processId = Base::Platform::Win32::loadProcedure<ProcessIdFunction>(module, "GetCurrentProcessId");
        static_cast<void>(context.expectTrue("known export resolves", processId != nullptr));

        if(processId != nullptr)
        {
            static_cast<void>(context.expectEq("resolved procedure returns this process id", GetCurrentProcessId(), processId()));
        }
    }
} // namespace

namespace GameWIP::Test::BasePlatform
{
    /// ------------------------------------------------------------
    /// Base validation suites
    /// ------------------------------------------------------------

    void registerSuites(GameWIP::TestSupport::Runner &runner)
    {
        runner.runSuite("Win32 procedure lookup", testProcedureLookup);
    }
} // namespace GameWIP::Test::BasePlatform
