@page test_support_public_api TestSupport public API guide

This page is the user-facing guide for the TestSupport public API. Header comments stay compact for IntelliSense; this page explains how the public types and helpers are intended to be used together.

## Include file

```cpp
#include "test_support/test_support.h"
```

TestSupport is generic. It must not depend on Logger, Assert, or engine systems.

## Type namespace

Passive value types live under `GameWIP::TestSupport::Types`. Active helpers and operations live directly under `GameWIP::TestSupport`.

## Reporting options

### `Types::ReportOptions`

Controls where report lines are written.

| Field | Meaning |
| --- | --- |
| `writeConsole` | Writes report lines to stdout when true. |
| `writeReport` | Writes report lines to `reportPath` when true. |
| `appendReport` | Appends to the report file instead of replacing it. |
| `reportPath` | Text report path. Parent directories are created when possible. |

Use this once at runner/context creation. It is runtime test behavior, not a compile-time build feature.

## Results and summaries

### `Types::Summary`

Tracks pass/fail/skip counts. `total()` returns all three counts. `ok()` is true when `failed == 0`.

### `Types::SuiteResult`

Stores one suite name, summary, and elapsed time. Returned by `Runner::runSuite()`.

### `Types::IterationMetric`

Stores a named iteration metric and computes nanoseconds per iteration. It is for reporting only; TestSupport does not apply hard performance thresholds.

## Context

`Context` is passed to suite code. It records categorized lines, expectations, failures, skips, and summaries.

Common recording calls:

```cpp
context.info("creating fixture");
context.pass("fixture initialized");
context.fail("fixture initialized", "missing data");
context.skip("manual-only case", "manual UI disabled");
context.metric("iterations=1000 nsPerCall=3.5");
context.stress("workers=8 attempts=100000");
context.summary("suite-specific summary text");
```

Expectation calls:

```cpp
context.expectTrue("loaded", loaded);
context.expectFalse("no error", hasError);
context.expectEq("count", 4, count);
context.expectNe("handle", nullptr, handle);
context.expectNear("position", expected, actual, 0.001);
context.expectContains("message", message, "ready");
context.expectFileContains("log contains marker", logPath, "READY");
context.expectFileOccurrenceCount("one fatal", logPath, "[Fatal]", 1);
```

Expectations record pass/fail and return a boolean. They do not abort the test process and they do not call engine assertion macros.

## Runner

`Runner` owns a shared report sink and aggregates suite results.

```cpp
GameWIP::TestSupport::Types::ReportOptions options;
GameWIP::TestSupport::Runner runner(options);

runner.runSuite(
    "Math",
    [](GameWIP::TestSupport::Context& context)
    {
        context.expectEq("one plus one", 2, 1 + 1);
    });

return runner.exitCode();
```

Use `Runner` for executable-level test programs. Use standalone `Context` only when a suite truly needs isolated report ownership.

## Sections

`Section` is an RAII helper for grouping large scenarios. It reports section start/end and duration without changing pass/fail counts.

```cpp
{
    GameWIP::TestSupport::Section section(context, "queue pressure");
    runQueuePressureScenario();
}
```

## Timer

`Timer` measures elapsed wall-clock time and nanoseconds per iteration.

```cpp
GameWIP::TestSupport::Timer timer;
runLoop();
context.metric("loopMs=" + std::to_string(timer.elapsedMilliseconds()));
```

Use metrics for visibility. Do not use them as hard thresholds unless a future policy explicitly adds baselines.

## File helpers

| API | Purpose |
| --- | --- |
| `readTextFile(path)` | Reads a text file; returns empty text when it cannot open. |
| `writeTextFile(path, text)` | Creates parent folders and writes text; throws on open/write failure. |
| `fileExists(path)` | Returns whether the path exists without throwing. |
| `fileContains(path, text)` | Returns true only when the file exists and contains the text. |
| `countFileOccurrences(path, text)` | Counts non-overlapping occurrences; empty text returns zero. |
| `createDirectories(path)` | Creates a directory tree when path is non-empty. |
| `removeIfExists(path)` | Removes a file/directory tree if it exists and ignores cleanup errors. |

These helpers are intentionally small and text-oriented. Use custom code for binary fixtures or complex parsing.

## Scoped environment helpers

`ScopedEnvironmentVariable` temporarily sets a variable and restores the previous state on destruction. `ScopedUnsetEnvironmentVariable` temporarily unsets a variable and restores it later.

On Windows, these helpers update both the CRT environment used by `std::getenv()` and the process environment inherited by child processes.

```cpp
{
    GameWIP::TestSupport::ScopedEnvironmentVariable action("GAMEWIP_MODE", "test");
    runScenario();
}
```

Environment mutation is process-global. Avoid overlapping conflicting scoped environment changes across threads.

## Child processes

Use child processes for tests that intentionally abort, break, hang, or need isolated environment changes.

```cpp
GameWIP::TestSupport::Types::ChildProcessOptions child;
child.executablePath = executablePath;
child.arguments = {"--child-test", "fatal"};
child.environment = {{"GAMEWIP_CHILD_MODE", "1"}};
child.timeout = std::chrono::milliseconds{5000};

const auto result = GameWIP::TestSupport::runChildProcess(child);
context.expectTrue("child failed as expected", result.exitedWithFailure());
```

`ChildProcessResult` reports exit code, timeout, test-requested termination, and optional captured stdout/stderr. It does not try to classify every possible crash reason portably.

## Manual checks

`promptManualCheck()` asks the user to answer Yes, No, or Skipped. Use it only when runtime options explicitly enable manual tests.

```cpp
const auto answer = GameWIP::TestSupport::promptManualCheck("Did the dialog show four buttons?");
```

Manual checks are for behavior code cannot verify automatically, such as visual UI and debugger interaction.

## Stress helpers

`StartGate` blocks worker threads until the test opens the gate. `StopFlag` provides a cooperative atomic stop request. `runWorkers()` starts a fixed number of worker threads, gives each worker its own callable copy, joins all workers, and rethrows captured worker exceptions.

```cpp
GameWIP::TestSupport::StartGate gate;
GameWIP::TestSupport::StopFlag stop;

std::thread stopper([&]
{
    std::this_thread::sleep_for(std::chrono::milliseconds{100});
    stop.requestStop();
});

GameWIP::TestSupport::runWorkers(4, [&](std::size_t workerIndex)
{
    gate.wait();
    while (!stop.stopRequested())
    {
        runOneWorkerStep(workerIndex);
    }
});

stopper.join();
```

Keep stress helpers generic. Logger-specific, Assert-specific, or engine-specific checks belong in the relevant test file.

## Related pages

- @ref test_support_quick_start
- @ref test_support_reports
- @ref test_support_child_processes
- @ref test_support_manual_tests
- @ref test_support_examples
