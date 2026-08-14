@page test_support_examples Examples

## Reporting

```cpp
namespace TS = GameWIP::TestSupport;

TS::Types::Reporting::Options options;
options.writeConsole = true;
options.reportPath = "logs/tests/example.txt";

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

Malformed UTF-8 returns `Types::InfrastructureError::EncodingFailed`. `writeTextFile()` performs that validation before creating parents or truncating the destination.

## Child process

```cpp
TS::Types::Process::Options options;
options.executablePath = executable;
options.arguments = {"--child"};
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
options.reportPath = "logs/tests/shared_report.txt";
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

const auto path = workspace.path() / "sample.txt";
if (!TS::writeTextFile(path, "alpha beta alpha").ok())
    return;

const TS::Types::BoolResult exists = TS::fileExists(path);
static_cast<void>(context.expectTrue("sample exists", exists.status.ok() && exists.value));
static_cast<void>(context.expectFileOccurrenceCount("alpha count", path, "alpha", 2));

{
    TS::ScopedCurrentPath currentPath(workspace.path());
    if (currentPath.status().ok())
        static_cast<void>(TS::writeTextFile("relative.txt", "fixture"));
}
```

The current directory is process-global; use `ScopedCurrentPath` only while the test owns relative-path resolution.

## Environment guards

```cpp
{
    TS::ScopedEnvironmentVariable value("GAMEWIP_TEST_MODE", "enabled");
    if (!value.status().ok())
        return;
}

{
    TS::ScopedUnsetEnvironmentVariable unset("GAMEWIP_TEST_MODE");
    if (!unset.status().ok())
        return;
}
```

Environment state is process-global. Overlapping guard lifetimes require caller coordination.

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
TS::StartGate gate;
TS::StopFlag stop;
TS::runWorkers(
    4,
    [&](std::size_t)
    {
        gate.wait();
        while (!stop.stopRequested())
            std::this_thread::yield();
    });
```

`runWorkers()` gives every worker its own callable copy, joins every successfully started thread, and rethrows one captured worker exception only after all workers have joined. Shared references captured by those callable copies still require their own synchronization.

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
