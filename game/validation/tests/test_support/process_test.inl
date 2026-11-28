/// @file process_test.inl
/// @brief Private TestSupport child-process tests.

/// @brief Verifies child launch, merged capture, truncation, timeout, environment, and process-tree cleanup.
void testChildProcesses(TestSupport::Context &context, const std::filesystem::path &executablePath, const TestSupportTestOptions &options)
{
    TestSupport::ScopedProcessState processState;
    static_cast<void>(context.expectTrue("Process-state scope construction succeeds", processState.status().ok()));

    if(!options.enableChildProcessTests)
    {
        context.skip("TestSupport child process tests", "disabled by TestSupportTestOptions");
        return;
    }

    // CTest and Windows process instrumentation can delay a fresh validation child well beyond
    // the library default. Explicit short waits below continue to exercise timeout enforcement.
    constexpr auto normalChildTimeout = 30s;

    // Reject malformed options before a child can start.
    const auto expectInvalidOptions = [&](std::string_view name, const TestSupport::Types::Process::Options &childOptions)
    {
        const TestSupport::Types::Process::Result result = TestSupport::runChildProcess(childOptions);
        static_cast<void>(context.expectEq(name, GameWIP::Status::Types::Code::InvalidArgument, result.status().code()));
        static_cast<void>(context.expectEq(
            std::string(name) + " leaves the child unstarted",
            TestSupport::Types::Process::Outcome::NotStarted,
            result.value().outcome));
    };

    // Validate argument and environment inputs.
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

    // Verify child environment overrides and removal.
    {
        const TestSupport::ScopedEnvironmentVariable parentUnset(processState, kChildUnsetVariable, "parent-value");
        static_cast<void>(context.expectTrue("Child environment parent fixture succeeds", parentUnset.status().ok()));

        TestSupport::Types::Process::Options childOptions;
        childOptions.executablePath = std::filesystem::path(executablePath);
        childOptions.arguments = {std::string(kEnvironmentChildArgument)};
        childOptions.environmentOverrides = {
            TestSupport::Types::Process::EnvironmentOverride{std::string(kChildSetVariable), std::string("child-value")},
            TestSupport::Types::Process::EnvironmentOverride{std::string(kChildUnsetVariable), std::nullopt}};
        childOptions.waitTimeout = normalChildTimeout;

        const TestSupport::Types::Process::Result result = TestSupport::runChildProcess(childOptions);
        static_cast<void>(context.expectTrue(
            "Child process environment exits successfully",
            result.ok() && result.value().outcome == TestSupport::Types::Process::Outcome::Exited && result.value().exitCode == 0));
        static_cast<void>(context.expectContains("Child process captures stdout", result.value().outputBytes, "child-set=child-value"));
        static_cast<void>(context.expectContains("Child process unsets environment", result.value().outputBytes, "child-unset=<unset>"));
    }

    // Confirm output capture can be disabled.
    {
        TestSupport::Types::Process::Options childOptions;
        childOptions.executablePath = std::filesystem::path(executablePath);
        childOptions.arguments = {std::string(kExitCodeChildArgument), "0"};
        childOptions.captureOutput = false;
        childOptions.waitTimeout = 5s;

        const TestSupport::Types::Process::Result result = TestSupport::runChildProcess(childOptions);
        static_cast<void>(context.expectTrue(
            "Child process capture can be disabled",
            result.ok() && result.value().outcome == TestSupport::Types::Process::Outcome::Exited && result.value().exitCode == 0));
        static_cast<void>(context.expectEq("Disabled capture leaves output empty", std::string(), result.value().outputBytes));
    }

    // Keep inherited-handle checks on the native platform.
    GameWIP::Test::TestSupportPlatform::testHandleInheritance(context, executablePath, kHandleInheritanceChildArgument);

    // Launch a child without inheriting the parent environment.
    {
        TestSupport::Types::Process::Options childOptions;
        childOptions.executablePath = std::filesystem::path(executablePath);
        childOptions.arguments = {std::string(kEnvironmentChildArgument)};
        childOptions.environmentOverrides = {
            TestSupport::Types::Process::EnvironmentOverride{std::string(kChildSetVariable), std::string("child-value")},
            TestSupport::Types::Process::EnvironmentOverride{std::string(kChildUnsetVariable), std::nullopt}};
        childOptions.inheritParentEnvironment = false;
        childOptions.waitTimeout = normalChildTimeout;

        const TestSupport::Types::Process::Result result = TestSupport::runChildProcess(childOptions);
        static_cast<void>(context.expectTrue(
            "Child process can disable parent environment inheritance",
            result.ok() && result.value().outcome == TestSupport::Types::Process::Outcome::Exited && result.value().exitCode == 0));
    }

    // Preserve nonzero child exit codes.
    {
        TestSupport::Types::Process::Options childOptions;
        childOptions.executablePath = std::filesystem::path(executablePath);
        childOptions.arguments = {std::string(kExitCodeChildArgument), "7"};
        childOptions.waitTimeout = normalChildTimeout;

        const TestSupport::Types::Process::Result result = TestSupport::runChildProcess(childOptions);
        static_cast<void>(context.expectTrue("Normal child exit has successful infrastructure", result.ok()));
        static_cast<void>(
            context.expectEq("Nonzero child reports exited outcome", TestSupport::Types::Process::Outcome::Exited, result.value().outcome));
        static_cast<void>(context.expectEq("Child process reports nonzero exit", std::uint32_t{7}, result.value().exitCode));
    }

    // Validate timeout options and indefinite waits.
    {
        TestSupport::Types::Process::Options childOptions;
        childOptions.executablePath = std::filesystem::path(executablePath);
        childOptions.arguments = {std::string(kExitCodeChildArgument), "0"};
        childOptions.waitTimeout = std::chrono::milliseconds{-1};
        expectInvalidOptions("Child process rejects negative wait timeout", childOptions);

        childOptions.waitTimeout = std::nullopt;
        const TestSupport::Types::Process::Result result = TestSupport::runChildProcess(childOptions);
        static_cast<void>(context.expectTrue(
            "Nullopt wait timeout waits indefinitely",
            result.ok() && result.value().outcome == TestSupport::Types::Process::Outcome::Exited && result.value().exitCode == 0));
    }

    // Verify the child receives its configured working directory.
    {
        TestSupport::Types::Process::Options childOptions;
        const std::filesystem::path absoluteExecutablePath = std::filesystem::absolute(std::filesystem::path(executablePath));
        childOptions.executablePath = absoluteExecutablePath;
        childOptions.arguments = {std::string(kCurrentPathChildArgument)};
        childOptions.workingDirectory = absoluteExecutablePath.parent_path();
        childOptions.waitTimeout = normalChildTimeout;
        const TestSupport::Types::Process::Result result = TestSupport::runChildProcess(childOptions);
        const std::string expectedDirectory = childOptions.workingDirectory->generic_string();
        static_cast<void>(context.expectTrue(
            "Child working directory succeeds",
            result.ok() && result.value().outcome == TestSupport::Types::Process::Outcome::Exited && result.value().exitCode == 0));
        static_cast<void>(context.expectContains("Child receives working directory", result.value().outputBytes, expectedDirectory));
    }

    // Check native exit-code boundaries separately from portable outcomes.
    GameWIP::Test::TestSupportPlatform::testExitCodeBoundaries(context, executablePath, kExitCodeChildArgument);

    // Report launch failures with the native diagnostic.
    {
        TestSupport::Types::Process::Options childOptions;
        childOptions.executablePath = std::filesystem::path(executablePath).parent_path() / "missing-gamewip-child.exe";
        childOptions.waitTimeout = normalChildTimeout;
        const TestSupport::Types::Process::Result result = TestSupport::runChildProcess(childOptions);
        static_cast<void>(
            context.expectEq("Missing child reports launch failure", GameWIP::Status::Types::Code::ProcessLaunchFailed, result.status().code()));
        static_cast<void>(
            context.expectEq("Missing child was not started", TestSupport::Types::Process::Outcome::NotStarted, result.value().outcome));
        static_cast<void>(context.expectTrue(
            "Missing child preserves a native diagnostic",
            result.status().diagnostic() != nullptr && result.status().diagnostic()->nativeCode.has_value()));
    }

    // Distinguish a child timeout from a launch failure.
    {
        TestSupport::Types::Process::Options childOptions;
        childOptions.executablePath = std::filesystem::path(executablePath);
        childOptions.arguments = {std::string(kSleepChildArgument)};
        childOptions.waitTimeout = 50ms;

        const TestSupport::Types::Process::Result result = TestSupport::runChildProcess(childOptions);
        static_cast<void>(context.expectTrue("Child process timeout has successful infrastructure", result.ok()));
        static_cast<void>(
            context.expectEq("Child process timeout is reported", TestSupport::Types::Process::Outcome::TimedOut, result.value().outcome));
    }

    // Exercise capture limits without repeating child setup.
    const auto runOutputChild = [&](std::size_t outputBytes, std::size_t captureLimit)
    {
        TestSupport::Types::Process::Options childOptions;
        childOptions.executablePath = std::filesystem::path(executablePath);
        childOptions.arguments = {std::string(kOutputChildArgument), std::to_string(outputBytes)};
        childOptions.maxCapturedOutputBytes = captureLimit;
        childOptions.waitTimeout = normalChildTimeout;
        return TestSupport::runChildProcess(childOptions);
    };

    // Captures below or at the limit retain all output.
    {
        const TestSupport::Types::Process::Result result = runOutputChild(4, 8);
        static_cast<void>(context.expectTrue(
            "Capture below limit succeeds",
            result.ok() && result.value().outcome == TestSupport::Types::Process::Outcome::Exited && result.value().exitCode == 0));
        static_cast<void>(context.expectEq("Capture below limit retains all bytes", std::size_t{4}, result.value().outputBytes.size()));
        static_cast<void>(context.expectFalse("Capture below limit is not truncated", result.value().outputTruncated));
    }

    {
        const TestSupport::Types::Process::Result result = runOutputChild(8, 8);
        static_cast<void>(context.expectEq("Capture at limit retains all bytes", std::size_t{8}, result.value().outputBytes.size()));
        static_cast<void>(context.expectFalse("Capture at limit is not truncated", result.value().outputTruncated));
    }

    // Captures above the limit retain a prefix and report truncation.
    {
        const TestSupport::Types::Process::Result result = runOutputChild(12, 8);
        static_cast<void>(context.expectTrue(
            "Capture above limit still succeeds",
            result.ok() && result.value().outcome == TestSupport::Types::Process::Outcome::Exited && result.value().exitCode == 0));
        static_cast<void>(context.expectEq("Capture above limit retains prefix", std::string(8, 'x'), result.value().outputBytes));
        static_cast<void>(context.expectTrue("Capture above limit reports truncation", result.value().outputTruncated));
    }

    // A zero limit still drains output to avoid blocking the child.
    {
        const TestSupport::Types::Process::Result result = runOutputChild(12, 0);
        static_cast<void>(context.expectTrue(
            "Zero capture limit still drains child",
            result.ok() && result.value().outcome == TestSupport::Types::Process::Outcome::Exited && result.value().exitCode == 0));
        static_cast<void>(context.expectTrue("Zero capture limit retains no bytes", result.value().outputBytes.empty()));
        static_cast<void>(context.expectTrue("Zero capture limit reports truncation", result.value().outputTruncated));
    }

    // A timed-out child also releases its descendants.
    {
        TestSupport::Types::Process::Options childOptions;
        childOptions.executablePath = std::filesystem::path(executablePath);
        childOptions.arguments = {std::string(kDescendantChildArgument)};
        childOptions.waitTimeout = 50ms;
        const TestSupport::Types::Process::Result result = TestSupport::runChildProcess(childOptions);
        static_cast<void>(context.expectTrue("Descendant process timeout has successful infrastructure", result.ok()));
        static_cast<void>(
            context.expectEq("Descendant process timeout is reported", TestSupport::Types::Process::Outcome::TimedOut, result.value().outcome));
    }
}
