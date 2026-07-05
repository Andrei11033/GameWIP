@page test_support_examples TestSupport examples

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
GameWIP::TestSupport::ScopedTemporaryDirectory workspace("file_example");
const std::filesystem::path path = workspace.path() / "sample.txt";
GameWIP::TestSupport::writeTextFile(path, "alpha beta alpha");

context.expectFileContains("sample contains beta", path, "beta");
context.expectFileOccurrenceCount("sample alpha count", path, "alpha", 2);
```

Occurrence counts are non-overlapping. Empty search text counts as zero occurrences. The sample file is removed with the workspace.

## Environment helpers

```cpp
{
    GameWIP::TestSupport::ScopedEnvironmentVariable variable(
        "MYAPP_TEST_MODE",
        "1");

    runScenarioThatReadsEnvironment();
}
```

On Windows, scoped environment helpers update both the CRT environment used by `std::getenv()` and the process environment inherited by child processes.

## Stress helpers

```cpp
GameWIP::TestSupport::StartGate gate;
GameWIP::TestSupport::StopFlag stop;

std::thread starter(
    [&gate]
    {
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
        gate.open();
    });

std::thread stopper(
    [&stop]
    {
        std::this_thread::sleep_for(std::chrono::milliseconds{100});
        stop.requestStop();
    });

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

starter.join();
stopper.join();
```

## Related pages

- @ref test_support_expectations
- @ref test_support_files_environment
- @ref test_support_child_processes
- @ref test_support_timing_stress
