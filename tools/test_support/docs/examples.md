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

@ref test_support_public_api
