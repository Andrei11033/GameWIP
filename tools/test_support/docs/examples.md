@page test_support_examples TestSupport examples

This page collects small examples for the TestSupport API.

## Sections and metrics

```cpp
runner.runSuite(
    "Stress",
    [](GameWIP::TestSupport::Context& context)
    {
        GameWIP::TestSupport::Section section(context, "worker startup");
        GameWIP::TestSupport::Timer timer;

        runExpensiveScenario();

        context.metric(
            "worker startup elapsedMs=" +
            std::to_string(timer.elapsedMilliseconds()));
    });
```

## File helpers

```cpp
const std::filesystem::path path = "logs/tests/sample.txt";
GameWIP::TestSupport::writeTextFile(path, "alpha beta alpha");

context.expectFileContains("sample contains beta", path, "beta");
context.expectFileOccurrenceCount("sample alpha count", path, "alpha", 2);
```

Occurrence counts are non-overlapping. Empty search text counts as zero occurrences.

## Environment helpers

```cpp
{
    GameWIP::TestSupport::ScopedEnvironmentVariable variable(
        "GAMEWIP_TEST_MODE",
        "1");

    runScenarioThatReadsEnvironment();
}
```

On Windows, scoped environment helpers update both the CRT environment used by `std::getenv()` and the process environment inherited by child processes.

## Stress helpers

```cpp
GameWIP::TestSupport::StartGate gate;
GameWIP::TestSupport::StopFlag stop;

std::thread stopper(
    [&stop]
    {
        std::this_thread::sleep_for(std::chrono::milliseconds{100});
        stop.requestStop();
    });

gate.open();
GameWIP::TestSupport::runWorkers(
    4,
    [&](std::size_t workerIndex)
    {
        gate.wait();
        while (!stop.stopRequested())
        {
            runOneWorkerStep(workerIndex);
        }
    });

stopper.join();
```

Examples should remain generic and avoid Logger-specific, Assert-specific, or engine-specific helper logic.


## Related pages

- @ref test_support_expectations
- @ref test_support_files_environment
- @ref test_support_child_processes
- @ref test_support_timing_stress
