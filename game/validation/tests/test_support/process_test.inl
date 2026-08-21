/// @file process_test.inl
/// @brief Private TestSupport correctness cases grouped by behavioral responsibility.

/// @brief Verifies child launch, merged capture, truncation, timeout, environment, and process-tree cleanup.
void testChildProcesses(TestSupport::Context &context, std::string_view executablePath, const TestSupportTestOptions &options)
{
    if (!options.enableChildProcessTests)
    {
        context.skip("TestSupport child process tests", "disabled by TestSupportTestOptions");
        return;
    }

    const auto expectInvalidOptions = [&](std::string_view name, const TestSupport::Types::Process::Options &childOptions)
    {
        const TestSupport::Types::Process::Result result = TestSupport::runChildProcess(childOptions);
        static_cast<void>(context.expectEq(name, TestSupport::Types::InfrastructureError::InvalidArgument, result.status.error));
        static_cast<void>(
            context.expectEq(std::string(name) + " leaves the child unstarted", TestSupport::Types::Process::Outcome::NotStarted, result.outcome));
    };

    {
        TestSupport::Types::Process::Options childOptions;
        childOptions.executablePath = std::filesystem::path(executablePath);
        childOptions.arguments = {std::string("invalid-\0-argument", 18)};
        expectInvalidOptions("Child process rejects embedded-null arguments", childOptions);

        childOptions.arguments = {std::string("\xFF", 1)};
        expectInvalidOptions("Child process rejects invalid UTF-8 arguments", childOptions);

        childOptions.arguments.clear();
        childOptions.environmentOverrides = {{"INVALID=NAME", "value"}};
        expectInvalidOptions("Child process rejects invalid environment names", childOptions);

        childOptions.environmentOverrides = {{"VALID_NAME", std::string("invalid-\0-value", 15)}};
        expectInvalidOptions("Child process rejects embedded-null environment values", childOptions);
    }

    {
        TestSupport::ScopedEnvironmentVariable parentUnset(kChildUnsetVariable, "parent-value");
        static_cast<void>(context.expectTrue("Child environment parent fixture succeeds", parentUnset.status().ok()));

        TestSupport::Types::Process::Options childOptions;
        childOptions.executablePath = std::filesystem::path(executablePath);
        childOptions.arguments = {std::string(kEnvironmentChildArgument)};
        childOptions.environmentOverrides = {
            TestSupport::Types::Process::EnvironmentOverride{std::string(kChildSetVariable), std::string("child-value")},
            TestSupport::Types::Process::EnvironmentOverride{std::string(kChildUnsetVariable), std::nullopt}};
        childOptions.timeout = 5s;

        const TestSupport::Types::Process::Result result = TestSupport::runChildProcess(childOptions);
        static_cast<void>(context.expectTrue(
            "Child process environment exits successfully",
            result.status.ok() && result.outcome == TestSupport::Types::Process::Outcome::Exited && result.exitCode == 0));
        static_cast<void>(context.expectContains("Child process captures stdout", result.outputBytes, "child-set=child-value"));
        static_cast<void>(context.expectContains("Child process unsets environment", result.outputBytes, "child-unset=<unset>"));
    }

    {
        TestSupport::Types::Process::Options childOptions;
        childOptions.executablePath = std::filesystem::path(executablePath);
        childOptions.arguments = {std::string(kExitCodeChildArgument), "0"};
        childOptions.captureOutput = false;
        childOptions.timeout = 5s;

        const TestSupport::Types::Process::Result result = TestSupport::runChildProcess(childOptions);
        static_cast<void>(context.expectTrue(
            "Child process capture can be disabled",
            result.status.ok() && result.outcome == TestSupport::Types::Process::Outcome::Exited && result.exitCode == 0));
        static_cast<void>(context.expectEq("Disabled capture leaves output empty", std::string(), result.outputBytes));
    }

#if defined(_WIN32)
    {
        SECURITY_ATTRIBUTES attributes{};
        attributes.nLength = sizeof(attributes);
        attributes.bInheritHandle = TRUE;
        const HANDLE unrelatedEvent = CreateEventW(&attributes, TRUE, FALSE, nullptr);
        static_cast<void>(
            context.expectTrue("Create inheritable sentinel handle succeeds", unrelatedEvent != nullptr && unrelatedEvent != INVALID_HANDLE_VALUE));
        if (unrelatedEvent != nullptr && unrelatedEvent != INVALID_HANDLE_VALUE)
        {
            TestSupport::Types::Process::Options childOptions;
            childOptions.executablePath = std::filesystem::path(executablePath);
            childOptions.arguments = {std::string(kHandleInheritanceChildArgument), std::to_string(reinterpret_cast<std::uintptr_t>(unrelatedEvent))};
            childOptions.timeout = 5s;

            const TestSupport::Types::Process::Result result = TestSupport::runChildProcess(childOptions);
            static_cast<void>(context.expectTrue(
                "Handle inheritance probe child succeeds",
                result.status.ok() && result.outcome == TestSupport::Types::Process::Outcome::Exited && result.exitCode == 0));
            static_cast<void>(context.expectEq(
                "Child does not inherit unrelated parent handles",
                static_cast<DWORD>(WAIT_TIMEOUT),
                WaitForSingleObject(unrelatedEvent, 0)));
            CloseHandle(unrelatedEvent);
        }
    }
#endif

    {
        TestSupport::Types::Process::Options childOptions;
        childOptions.executablePath = std::filesystem::path(executablePath);
        childOptions.arguments = {std::string(kEnvironmentChildArgument)};
        childOptions.environmentOverrides = {
            TestSupport::Types::Process::EnvironmentOverride{std::string(kChildSetVariable), std::string("child-value")},
            TestSupport::Types::Process::EnvironmentOverride{std::string(kChildUnsetVariable), std::nullopt}};
        childOptions.inheritParentEnvironment = false;
        childOptions.timeout = 5s;

        const TestSupport::Types::Process::Result result = TestSupport::runChildProcess(childOptions);
        static_cast<void>(context.expectTrue(
            "Child process can disable parent environment inheritance",
            result.status.ok() && result.outcome == TestSupport::Types::Process::Outcome::Exited && result.exitCode == 0));
    }

    {
        TestSupport::Types::Process::Options childOptions;
        childOptions.executablePath = std::filesystem::path(executablePath);
        childOptions.arguments = {std::string(kExitCodeChildArgument), "7"};
        childOptions.timeout = 5s;

        const TestSupport::Types::Process::Result result = TestSupport::runChildProcess(childOptions);
        static_cast<void>(context.expectTrue("Normal child exit has successful infrastructure", result.status.ok()));
        static_cast<void>(context.expectEq("Nonzero child reports exited outcome", TestSupport::Types::Process::Outcome::Exited, result.outcome));
        static_cast<void>(context.expectEq("Child process reports nonzero exit", std::uint32_t{7}, result.exitCode));
    }

#if defined(_WIN32)
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
        for (const ExitBoundary &boundary : boundaries)
        {
            TestSupport::Types::Process::Options childOptions;
            childOptions.executablePath = std::filesystem::path(executablePath);
            childOptions.arguments = {std::string(kExitCodeChildArgument), std::string(boundary.argument)};
            childOptions.timeout = 5s;

            const TestSupport::Types::Process::Result result = TestSupport::runChildProcess(childOptions);
            static_cast<void>(context.expectTrue("Boundary child exit has successful infrastructure", result.status.ok()));
            static_cast<void>(
                context.expectEq("Boundary child reports exited outcome", TestSupport::Types::Process::Outcome::Exited, result.outcome));
            static_cast<void>(context.expectEq("Boundary child exit preserves all native bits", boundary.expected, result.exitCode));
        }
    }
#endif

    {
        TestSupport::Types::Process::Options childOptions;
        childOptions.executablePath = std::filesystem::path(executablePath).parent_path() / "missing-gamewip-child.exe";
        childOptions.timeout = 5s;
        const TestSupport::Types::Process::Result result = TestSupport::runChildProcess(childOptions);
        static_cast<void>(context.expectEq(
            "Missing child reports launch failure",
            TestSupport::Types::InfrastructureError::ProcessLaunchFailed,
            result.status.error));
        static_cast<void>(context.expectEq("Missing child was not started", TestSupport::Types::Process::Outcome::NotStarted, result.outcome));
        static_cast<void>(context.expectTrue("Missing child preserves a native diagnostic", result.status.nativeCode != 0));
    }

    {
        TestSupport::Types::Process::Options childOptions;
        childOptions.executablePath = std::filesystem::path(executablePath);
        childOptions.arguments = {std::string(kSleepChildArgument)};
        childOptions.timeout = 50ms;

        const TestSupport::Types::Process::Result result = TestSupport::runChildProcess(childOptions);
        static_cast<void>(context.expectTrue("Child process timeout has successful infrastructure", result.status.ok()));
        static_cast<void>(context.expectEq("Child process timeout is reported", TestSupport::Types::Process::Outcome::TimedOut, result.outcome));
    }

    const auto runOutputChild = [&](std::size_t outputBytes, std::size_t captureLimit)
    {
        TestSupport::Types::Process::Options childOptions;
        childOptions.executablePath = std::filesystem::path(executablePath);
        childOptions.arguments = {std::string(kOutputChildArgument), std::to_string(outputBytes)};
        childOptions.maxCapturedOutputBytes = captureLimit;
        childOptions.timeout = 5s;
        return TestSupport::runChildProcess(childOptions);
    };

    {
        const TestSupport::Types::Process::Result result = runOutputChild(4, 8);
        static_cast<void>(context.expectTrue(
            "Capture below limit succeeds",
            result.status.ok() && result.outcome == TestSupport::Types::Process::Outcome::Exited && result.exitCode == 0));
        static_cast<void>(context.expectEq("Capture below limit retains all bytes", std::size_t{4}, result.outputBytes.size()));
        static_cast<void>(context.expectFalse("Capture below limit is not truncated", result.outputTruncated));
    }

    {
        const TestSupport::Types::Process::Result result = runOutputChild(8, 8);
        static_cast<void>(context.expectEq("Capture at limit retains all bytes", std::size_t{8}, result.outputBytes.size()));
        static_cast<void>(context.expectFalse("Capture at limit is not truncated", result.outputTruncated));
    }

    {
        const TestSupport::Types::Process::Result result = runOutputChild(12, 8);
        static_cast<void>(context.expectTrue(
            "Capture above limit still succeeds",
            result.status.ok() && result.outcome == TestSupport::Types::Process::Outcome::Exited && result.exitCode == 0));
        static_cast<void>(context.expectEq("Capture above limit retains prefix", std::string(8, 'x'), result.outputBytes));
        static_cast<void>(context.expectTrue("Capture above limit reports truncation", result.outputTruncated));
    }

    {
        const TestSupport::Types::Process::Result result = runOutputChild(12, 0);
        static_cast<void>(context.expectTrue(
            "Zero capture limit still drains child",
            result.status.ok() && result.outcome == TestSupport::Types::Process::Outcome::Exited && result.exitCode == 0));
        static_cast<void>(context.expectTrue("Zero capture limit retains no bytes", result.outputBytes.empty()));
        static_cast<void>(context.expectTrue("Zero capture limit reports truncation", result.outputTruncated));
    }

    {
        TestSupport::Types::Process::Options childOptions;
        childOptions.executablePath = std::filesystem::path(executablePath);
        childOptions.arguments = {std::string(kDescendantChildArgument)};
        childOptions.timeout = 50ms;
        const TestSupport::Types::Process::Result result = TestSupport::runChildProcess(childOptions);
        static_cast<void>(context.expectTrue("Descendant process timeout has successful infrastructure", result.status.ok()));
        static_cast<void>(context.expectEq("Descendant process timeout is reported", TestSupport::Types::Process::Outcome::TimedOut, result.outcome));
    }

#if TEST_SUPPORT_INTERNAL_TEST_HOOKS
    {
        using FailurePoint = TestSupport::TestHooks::ChildProcessFailurePoint;
        struct FailureCase
        {
            FailurePoint point;
            TestSupport::Types::InfrastructureError error;
            TestSupport::Types::Process::Outcome outcome;
        };

        constexpr std::array failureCases{
            FailureCase{
                FailurePoint::Allocation,
                TestSupport::Types::InfrastructureError::OutOfMemory,
                TestSupport::Types::Process::Outcome::NotStarted},
            FailureCase{
                FailurePoint::Unsupported,
                TestSupport::Types::InfrastructureError::Unsupported,
                TestSupport::Types::Process::Outcome::NotStarted},
            FailureCase{
                FailurePoint::Platform,
                TestSupport::Types::InfrastructureError::PlatformFailure,
                TestSupport::Types::Process::Outcome::NotStarted},
            FailureCase{
                FailurePoint::ProcessSetup,
                TestSupport::Types::InfrastructureError::ProcessSetupFailed,
                TestSupport::Types::Process::Outcome::NotStarted},
            FailureCase{
                FailurePoint::HandleSetup,
                TestSupport::Types::InfrastructureError::ProcessSetupFailed,
                TestSupport::Types::Process::Outcome::NotStarted},
            FailureCase{
                FailurePoint::PipeCreation,
                TestSupport::Types::InfrastructureError::PipeCreationFailed,
                TestSupport::Types::Process::Outcome::NotStarted},
            FailureCase{
                FailurePoint::ProcessLaunch,
                TestSupport::Types::InfrastructureError::ProcessLaunchFailed,
                TestSupport::Types::Process::Outcome::NotStarted},
            FailureCase{
                FailurePoint::JobAssignment,
                TestSupport::Types::InfrastructureError::ProcessSetupFailed,
                TestSupport::Types::Process::Outcome::TerminatedDuringCleanup},
            FailureCase{
                FailurePoint::CaptureSetup,
                TestSupport::Types::InfrastructureError::CaptureFailed,
                TestSupport::Types::Process::Outcome::TerminatedDuringCleanup},
            FailureCase{
                FailurePoint::ThreadCreation,
                TestSupport::Types::InfrastructureError::CaptureFailed,
                TestSupport::Types::Process::Outcome::TerminatedDuringCleanup},
            FailureCase{
                FailurePoint::ThreadResume,
                TestSupport::Types::InfrastructureError::ProcessSetupFailed,
                TestSupport::Types::Process::Outcome::TerminatedDuringCleanup},
            FailureCase{
                FailurePoint::CaptureRead,
                TestSupport::Types::InfrastructureError::CaptureFailed,
                TestSupport::Types::Process::Outcome::Exited},
            FailureCase{
                FailurePoint::Wait,
                TestSupport::Types::InfrastructureError::WaitFailed,
                TestSupport::Types::Process::Outcome::TerminatedDuringCleanup},
            FailureCase{
                FailurePoint::ProcessInspection,
                TestSupport::Types::InfrastructureError::ProcessInspectionFailed,
                TestSupport::Types::Process::Outcome::OutcomeUnavailable},
            FailureCase{
                FailurePoint::ProcessCleanup,
                TestSupport::Types::InfrastructureError::ProcessCleanupFailed,
                TestSupport::Types::Process::Outcome::Exited},
        };

        TestSupport::TestHooks::reset();
        for (std::size_t index = 0; index < failureCases.size(); ++index)
        {
            const FailureCase &failureCase = failureCases[index];
            const std::uint64_t nativeCode = 0x1000u + index;
            TestSupport::TestHooks::forceNextChildProcessFailure(failureCase.point, nativeCode);

            TestSupport::Types::Process::Options childOptions;
            childOptions.executablePath = std::filesystem::path(executablePath);
            childOptions.arguments = {std::string(kExitCodeChildArgument), "0"};
            childOptions.timeout = 5s;
            const TestSupport::Types::Process::Result result = TestSupport::runChildProcess(childOptions);

            static_cast<void>(context.expectEq("Injected child failure reports its category", failureCase.error, result.status.error));
            static_cast<void>(context.expectEq("Injected child failure preserves native code", nativeCode, result.status.nativeCode));
            static_cast<void>(context.expectEq("Injected child failure reports its outcome", failureCase.outcome, result.outcome));
        }
        TestSupport::TestHooks::reset();
    }
#else
    context.pass("TestSupport child-process hooks skipped because TEST_SUPPORT_INTERNAL_TEST_HOOKS=0");
#endif
}
