/// @file cancellation_test.inl
/// @brief Win32 Terminal cancellation tests.

#if TERMINAL_INTERNAL_TEST_HOOKS

/// ------------------------------------------------------------
/// Cancellation behavior
/// ------------------------------------------------------------

/// @brief Runs a child test for cancellation when the stop callback cannot signal its event.
int runCancellationSignalFailureChildMode()
{
    Hooks::reset();
    Hooks::forceNextCancellationSignalFailure();
    std::stop_source stopSource;
    const std::size_t initialWaits = Hooks::Win32::consoleWaitCallCount();
    Hooks::Win32::Win32ConsoleWaitResult result;
    std::thread reader(
        [&]
        {
            result = Hooks::Win32::waitForConsoleRecordForTest(std::chrono::milliseconds{-1}, stopSource.get_token());
        });
    const auto setupDeadline = std::chrono::steady_clock::now() + std::chrono::seconds{1};

    while(Hooks::Win32::consoleWaitCallCount() == initialWaits && std::chrono::steady_clock::now() < setupDeadline)
    {
        std::this_thread::yield();
    }

    if(Hooks::Win32::consoleWaitCallCount() == initialWaits)
    {
        stopSource.request_stop();
        reader.join();
        return 3;
    }

    std::cout << "Native wait reached; requesting cancellation.\n" << std::flush;
    const auto cancellationStarted = std::chrono::steady_clock::now();
    stopSource.request_stop();
    reader.join();
    const auto cancellationElapsed = std::chrono::steady_clock::now() - cancellationStarted;
    const bool passed =
        result.status.ok() && result.outcome == Terminal::Types::Input::ReadOutcome::Cancelled && cancellationElapsed < std::chrono::seconds{2};
    std::cout << std::format(
        "Cancellation code={} outcome={} elapsedMs={}\n",
        IO::errorCodeName(result.status.code),
        static_cast<int>(result.outcome),
        std::chrono::duration_cast<std::chrono::milliseconds>(cancellationElapsed).count());
    Hooks::reset();
    return passed ? 0 : 1;
}

/// ------------------------------------------------------------
/// Cancellation failure handling
/// ------------------------------------------------------------

/// @brief Verifies bounded cancellation after the native signal fails.
void testCancellationSignalFailure(TestSupport::Context &context, const std::filesystem::path &executablePath)
{
    Hooks::reset();
    TestSupport::Types::Process::Options child;
    child.executablePath = std::filesystem::path(executablePath);
    child.arguments = {std::string(kCancellationSignalFailureChildArgument)};
    // Startup and profile teardown are separate from the child's post-stop cancellation bound.
    child.waitTimeout = std::chrono::seconds{30};
    child.captureOutput = true;
    const TestSupport::Types::Process::Result childResult = TestSupport::runChildProcess(child);
    static_cast<void>(context.expectTrue("infinite cancellation child infrastructure succeeds", childResult.status.ok()));
    static_cast<void>(context.expectEq("infinite cancellation child exits", TestSupport::Types::Process::Outcome::Exited, childResult.outcome));

    if(childResult.outcome == TestSupport::Types::Process::Outcome::Exited)
    {
        static_cast<void>(context.expectEq("infinite cancellation child returns success", std::uint32_t{0}, childResult.exitCode));
    }

    if(!childResult.status.ok() || childResult.outcome != TestSupport::Types::Process::Outcome::Exited || childResult.exitCode != 0)
    {
        context.info(
            std::format(
                "{} outcome={} exit={}\n{}",
                TestSupport::formatInfrastructureStatus(childResult.status),
                static_cast<int>(childResult.outcome),
                childResult.exitCode,
                childResult.outputBytes));
    }

    const auto run = [&context](std::string_view label, bool forceSignalFailure)
    {
        std::stop_source stopSource;

        if(forceSignalFailure)
        {
            Hooks::forceNextCancellationSignalFailure();
        }

        const std::size_t initialWaits = Hooks::Win32::consoleWaitCallCount();
        Hooks::Win32::Win32ConsoleWaitResult result;
        std::thread reader(
            [&]
            {
                result = Hooks::Win32::waitForConsoleRecordForTest(std::chrono::milliseconds{500}, stopSource.get_token());
            });
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};

        while(Hooks::Win32::consoleWaitCallCount() == initialWaits && std::chrono::steady_clock::now() < deadline)
        {
            std::this_thread::yield();
        }
        static_cast<void>(
            context.expectTrue(std::format("{} reaches a native wait before stop", label), Hooks::Win32::consoleWaitCallCount() != initialWaits));
        stopSource.request_stop();
        reader.join();
        static_cast<void>(
            context.expectEq(std::format("{} returns Cancelled", label), Terminal::Types::Input::ReadOutcome::Cancelled, result.outcome));
        static_cast<void>(context.expectTrue(std::format("{} status succeeds", label), result.status.ok()));
    };

    Hooks::reset();
    run("finite wait with signal failure", true);

    Hooks::reset();
    Hooks::forceNextCancellationSignalFailure();
    run("first one-shot signal callback", false);
    run("second one-shot signal callback", false);
    Hooks::reset();
}

/// @brief Verifies native cancellation-event reset failure is reported before any blocking wait.
void testCancellationResetFailure(TestSupport::Context &context)
{
    Hooks::reset();
    const std::stop_source stopSource;
    Hooks::forceNextCancellationResetFailure();
    const Hooks::Win32::Win32ConsoleWaitResult failed =
        Hooks::Win32::waitForConsoleRecordForTest(std::chrono::milliseconds{0}, stopSource.get_token());
    static_cast<void>(context.expectEq("ResetEvent failure is returned", ErrorCode::NativeFailure, failed.status.code));
    static_cast<void>(
        context.expectFalse("ResetEvent failure is not cancellation", failed.outcome == Terminal::Types::Input::ReadOutcome::Cancelled));
    static_cast<void>(context.expectEq("ResetEvent failure enters no native wait", std::size_t{0}, Hooks::Win32::consoleWaitCallCount()));

    const Hooks::Win32::Win32ConsoleWaitResult retry =
        Hooks::Win32::waitForConsoleRecordForTest(std::chrono::milliseconds{0}, stopSource.get_token());
    static_cast<void>(context.expectTrue("cancellation wait can be retried", retry.status.ok()));
    static_cast<void>(context.expectTrue("next operation reaches normal wait path", Hooks::Win32::consoleWaitCallCount() > 0));
    Hooks::reset();
}
#endif
