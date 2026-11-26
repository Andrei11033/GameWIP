/// @file process.cpp
/// @brief Win32 TestSupport process validation.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "validation/tests/test_support/platform.h"

#include "test_support/test_support.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <thread>

#include <windows.h>

namespace GameWIP::Test::TestSupportPlatform
{
    /// ------------------------------------------------------------
    /// Native child modes
    /// ------------------------------------------------------------

    void signalChildEvent(std::uintptr_t eventHandle) noexcept
    {
        static_cast<void>(SetEvent(reinterpret_cast<HANDLE>(eventHandle)));
    }

    int runDescendantChild(const std::filesystem::path &executablePath, std::string_view childArgument)
    {
        const std::wstring argument(childArgument.begin(), childArgument.end());
        std::wstring commandLine = L"\"" + executablePath.native() + L"\" " + argument;
        STARTUPINFOW startupInfo{};
        startupInfo.cb = sizeof(startupInfo);
        PROCESS_INFORMATION processInfo{};

        if(CreateProcessW(
               executablePath.c_str(),
               commandLine.data(),
               nullptr,
               nullptr,
               TRUE,
               CREATE_NO_WINDOW,
               nullptr,
               nullptr,
               &startupInfo,
               &processInfo) == FALSE)
        {
            return 11;
        }

        CloseHandle(processInfo.hThread);
        CloseHandle(processInfo.hProcess);

        // Keep the child alive so its parent can test descendant cleanup.
        std::this_thread::sleep_for(std::chrono::seconds{5});
        return 0;
    }

    /// ------------------------------------------------------------
    /// Handle inheritance
    /// ------------------------------------------------------------

    void testHandleInheritance(TestSupport::Context &context, const std::filesystem::path &executablePath, std::string_view childArgument)
    {
        SECURITY_ATTRIBUTES attributes{};
        attributes.nLength = sizeof(attributes);
        attributes.bInheritHandle = TRUE;

        const HANDLE unrelatedEvent = CreateEventW(&attributes, TRUE, FALSE, nullptr);
        static_cast<void>(
            context.expectTrue("Create inheritable sentinel handle succeeds", unrelatedEvent != nullptr && unrelatedEvent != INVALID_HANDLE_VALUE));

        if(unrelatedEvent == nullptr || unrelatedEvent == INVALID_HANDLE_VALUE)
        {
            return;
        }

        TestSupport::Types::Process::Options childOptions;
        childOptions.executablePath = executablePath;
        childOptions.arguments = {std::string(childArgument), std::to_string(reinterpret_cast<std::uintptr_t>(unrelatedEvent))};
        childOptions.waitTimeout = std::chrono::seconds{30};

        const TestSupport::Types::Process::Result result = TestSupport::runChildProcess(childOptions);
        static_cast<void>(context.expectTrue(
            "Handle inheritance probe child succeeds",
            result.ok() && result.value().outcome == TestSupport::Types::Process::Outcome::Exited && result.value().exitCode == 0));
        static_cast<void>(context.expectEq(
            "Child does not inherit unrelated parent handles",
            static_cast<DWORD>(WAIT_TIMEOUT),
            WaitForSingleObject(unrelatedEvent, 0)));

        CloseHandle(unrelatedEvent);
    }

    /// ------------------------------------------------------------
    /// Process exit codes
    /// ------------------------------------------------------------

    void testExitCodeBoundaries(TestSupport::Context &context, const std::filesystem::path &executablePath, std::string_view childArgument)
    {
        struct ExitBoundary
        {
            std::string_view argument;
            std::uint32_t expected;
        };

        constexpr std::array boundaries{
            ExitBoundary{"2147483647", 0x7fffffffu},
            ExitBoundary{"-2147483648", 0x80000000u},
            ExitBoundary{"-1", 0xffffffffu},
        };

        for(const ExitBoundary &boundary : boundaries)
        {
            TestSupport::Types::Process::Options childOptions;
            childOptions.executablePath = executablePath;
            childOptions.arguments = {std::string(childArgument), std::string(boundary.argument)};
            childOptions.waitTimeout = std::chrono::seconds{30};

            const TestSupport::Types::Process::Result result = TestSupport::runChildProcess(childOptions);
            static_cast<void>(context.expectTrue("Boundary child exit has successful infrastructure", result.ok()));
            static_cast<void>(
                context.expectEq("Boundary child reports exited outcome", TestSupport::Types::Process::Outcome::Exited, result.value().outcome));
            static_cast<void>(context.expectEq("Boundary child exit preserves all native bits", boundary.expected, result.value().exitCode));
        }
    }
} // namespace GameWIP::Test::TestSupportPlatform
