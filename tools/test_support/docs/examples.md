@page test_support_examples Examples

These examples show the common building blocks in isolation: reports, guarded
process state, child execution, manual observations, and concurrent stress.

## Reporting

```cpp
namespace TS = GameWIP::TestSupport;

TS::Types::Reporting::Options options;
options.writeConsole = true;
options.reportPath = "logs/validation/example.txt";

TS::Runner runner(options);
runner.runSuite(
    "Example",
    [](TS::Context& context)
    {
        static_cast<void>(context.expectTrue("ready", true));
    });
return runner.exitCode();
```

## UTF-8 fixture

```cpp
const auto write = TS::writeTextFile(path, "Grüße λ");
if (!write.ok())
    return;

const TS::Types::TextResult read = TS::readTextFile(path);
if (read.status.ok())
    context.expectContains("fixture", read.text, "λ");
```

Malformed UTF-8 returns `Types::InfrastructureError::EncodingFailed`. `writeTextFile()` performs that validation before creating parents or truncating
the destination.

## Child process

```cpp
TS::Types::Process::Options options;
options.executablePath = executable;
options.arguments = {"--child"};
options.waitTimeout = std::chrono::seconds{5}; // std::nullopt waits indefinitely; negative values are invalid.
options.workingDirectory = workspace.path(); // Child-only; the parent current directory is unchanged.
options.environmentOverrides = {
    TS::Types::Process::EnvironmentOverride{"MODE", std::string{"test"}},
    TS::Types::Process::EnvironmentOverride{"REMOVE_ME", std::nullopt},
};

const TS::Types::Process::Result result = TS::runChildProcess(options);
if (!result.status.ok())
    context.fail("child infrastructure", TS::formatInfrastructureStatus(result.status));
else if (result.outcome == TS::Types::Process::Outcome::Exited)
    context.expectEq("child exit", std::uint32_t{0}, result.exitCode);
```

`result.outputBytes` is arbitrary combined stdout/stderr bytes. A caller may decode it only when the child protocol itself guarantees an encoding.

## Standalone context and append mode

```cpp
TS::Types::Reporting::Options options;
options.reportPath = "logs/validation/shared_report.txt";
options.appendReport = true;

TS::Context context("Standalone", options);
context.info("standalone validation started");
context.pass("setup complete");
```

## Temporary files and current-directory scope

```cpp
TS::ScopedTemporaryDirectory workspace("fixture");
if (!workspace.status().ok())
    return;

TS::ScopedProcessState processState;
if (!processState.status().ok())
    return;

const auto path = workspace.path() / "sample.txt";
if (!TS::writeTextFile(path, "alpha beta alpha").ok())
    return;

const TS::Types::BoolResult exists = TS::fileExists(path);
static_cast<void>(context.expectTrue("sample exists", exists.status.ok() && exists.value));
static_cast<void>(context.expectFileOccurrenceCount("alpha count", path, "alpha", 2));

{
    TS::ScopedCurrentPath currentPath(processState, workspace.path());
    if (currentPath.status().ok())
        static_cast<void>(TS::writeTextFile("relative.txt", "fixture"));
}
```

The current directory is process-global; use `ScopedCurrentPath` only while the test owns relative-path resolution.

## Environment guards

```cpp
{
    TS::ScopedEnvironmentVariable value(processState, "GAMEWIP_TEST_MODE", "enabled");
    if (!value.status().ok())
        return;
}

{
    TS::ScopedUnsetEnvironmentVariable unset(processState, "GAMEWIP_TEST_MODE");
    if (!unset.status().ok())
        return;
}
```

Environment and current-directory state are process-global. Keep one `ScopedProcessState` alive for the complete guard lifetime; use `restore()` when
the restoration status must be observed before scope exit.

## Manual checks and section timing

```cpp
const TS::Types::Reporting::ManualAnswer answer =
    TS::promptManualCheck("Did the expected UI appear?");
if (answer == TS::Types::Reporting::ManualAnswer::No)
    context.fail("manual UI", "expected UI was not observed");

{
    TS::Section section(context, "startup");
    TS::Timer timer;
    context.metric("startupMs=" + std::to_string(timer.elapsedMilliseconds()));
}
```

## Stress coordination

```cpp
std::atomic_size_t visits{0};
TS::StartGate gate;

std::jthread coordinator([&gate]
{
    gate.open();
});

TS::runWorkers(
    4,
    [&](std::size_t)
    {
        gate.wait();
        visits.fetch_add(1, std::memory_order_relaxed);
    });
```

This finite example uses a coordinator to open the gate while `runWorkers()` is
starting and joining its workers. `runWorkers()` gives every worker its own
callable copy,
joins every successfully started thread, and rethrows one captured worker
exception only after all workers have joined. Shared references captured by
those callable copies still require their own synchronization. See
@ref test_support_timing_stress for `StopFlag` and more advanced coordination.

```cpp
try
{
    TS::runWorkers(
        2,
        [](std::size_t workerIndex)
        {
            if (workerIndex == 1)
                throw std::runtime_error("worker failed");
        });
}
catch (const std::runtime_error&)
{
    // Expected worker failure was propagated after join.
}
```

@ref test_support_public_api
