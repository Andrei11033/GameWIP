@page test_support_examples TestSupport examples

Each example is complete apart from the linked TestSupport library.

## Runner, report options, and dependent checks

```cpp
#include "test_support/test_support.h"

int main()
{
    namespace TS = GameWIP::TestSupport;

    TS::Types::ReportOptions options;
    options.reportPath = "logs/tests/example_report.txt";
    options.consoleVerbosity = TS::Types::ConsoleVerbosity::Concise;

    TS::Runner runner(options);
    runner.runSuite(
        "Parser",
        [](TS::Context& context)
        {
            const bool fixtureReady = true;
            if (!context.expectTrue("fixture ready", fixtureReady))
            {
                return;
            }

            static_cast<void>(
                context.expectContains("token present", "alpha beta", "beta"));
        });

    return runner.exitCode();
}
```

## Standalone context and append mode

```cpp
#include "test_support/test_support.h"

int main()
{
    namespace TS = GameWIP::TestSupport;

    TS::Types::ReportOptions options;
    options.reportPath = "logs/tests/shared_report.txt";
    options.appendReport = true;

    TS::Context context("Standalone", options);
    context.info("standalone validation started");
    context.pass("setup complete");
    return context.ok() ? 0 : 1;
}
```

## Temporary files and file expectations

```cpp
#include "test_support/test_support.h"

int main()
{
    namespace TS = GameWIP::TestSupport;

    TS::Types::ReportOptions options;
    options.writeConsole = false;
    options.writeReport = false;
    TS::Context context("Files", options);

    TS::ScopedTemporaryDirectory workspace("file_example");
    const auto path = workspace.path() / "sample.txt";
    TS::writeTextFile(path, "alpha beta alpha");

    static_cast<void>(context.expectTrue("sample exists", TS::fileExists(path)));
    static_cast<void>(context.expectFileContains("sample contains beta", path, "beta"));
    static_cast<void>(
        context.expectFileOccurrenceCount("sample alpha count", path, "alpha", 2));
    return context.ok() ? 0 : 1;
}
```

## Current-directory scope

```cpp
#include "test_support/test_support.h"

#include <filesystem>

int main()
{
    namespace TS = GameWIP::TestSupport;

    TS::ScopedTemporaryDirectory workspace("relative_path");
    {
        TS::ScopedCurrentPath currentPath(workspace.path());
        TS::writeTextFile("relative.txt", "fixture");
    }

    return TS::fileExists(workspace.path() / "relative.txt") ? 0 : 1;
}
```

The process current directory is global; use this pattern only while the test owns relative-path resolution.

## Environment set and unset guards

```cpp
#include "test_support/test_support.h"

#include <cstdlib>
#include <string_view>

int main()
{
    namespace TS = GameWIP::TestSupport;
    constexpr std::string_view name = "GAMEWIP_TESTSUPPORT_EXAMPLE";

    {
        TS::ScopedEnvironmentVariable value(name, "enabled");
        const char* current = std::getenv(name.data());
        if (current == nullptr || std::string_view(current) != "enabled")
        {
            return 1;
        }
    }

    {
        TS::ScopedUnsetEnvironmentVariable unset(name);
        if (std::getenv(name.data()) != nullptr)
        {
            return 1;
        }
    }

    return 0;
}
```

## Child process and result interpretation

```cpp
#include "test_support/test_support.h"

#include <chrono>

int main(int argc, char** argv)
{
    namespace TS = GameWIP::TestSupport;

    if (argc > 1)
    {
        return 7;
    }

    TS::Types::ChildProcessOptions options;
    options.executablePath = argv[0];
    options.arguments = {"--child"};
    options.timeout = std::chrono::seconds{5};
    options.maxCapturedOutputBytes = 64 * 1024;

    const TS::Types::ChildProcessResult result = TS::runChildProcess(options);
    if (result.timedOut || result.wasTerminatedByTest || result.exitCode == -1)
    {
        return 1;
    }

    return result.exitCode == 7 ? 0 : 1;
}
```

## Manual answer handling

```cpp
#include "test_support/test_support.h"

int main()
{
    using GameWIP::TestSupport::Types::ManualAnswer;
    const ManualAnswer answer = GameWIP::TestSupport::promptManualCheck(
        "Did the expected UI appear?");

    return answer == ManualAnswer::No ? 1 : 0;
}
```

## Section and timer metrics

```cpp
#include "test_support/test_support.h"

int main()
{
    namespace TS = GameWIP::TestSupport;

    TS::Types::ReportOptions options;
    options.writeReport = false;
    TS::Context context("Metrics", options);

    {
        TS::Section section(context, "startup");
        TS::Timer timer;
        context.metric("startupMs=" + std::to_string(timer.elapsedMilliseconds()));
    }

    return context.ok() ? 0 : 1;
}
```

## Gate, stop flag, and workers

```cpp
#include "test_support/test_support.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <thread>

int main()
{
    namespace TS = GameWIP::TestSupport;

    TS::StartGate gate;
    TS::StopFlag stop;
    std::atomic_size_t iterations{0};

    std::thread coordinator(
        [&]
        {
            gate.open();
            std::this_thread::sleep_for(std::chrono::milliseconds{10});
            stop.requestStop();
        });

    TS::runWorkers(
        4,
        [&](std::size_t)
        {
            gate.wait();
            while (!stop.stopRequested())
            {
                iterations.fetch_add(1, std::memory_order_relaxed);
            }
        });

    coordinator.join();
    return iterations.load(std::memory_order_relaxed) > 0 ? 0 : 1;
}
```

## Worker exception propagation

```cpp
#include "test_support/test_support.h"

#include <cstddef>
#include <stdexcept>

int main()
{
    try
    {
        GameWIP::TestSupport::runWorkers(
            2,
            [](std::size_t workerIndex)
            {
                if (workerIndex == 1)
                {
                    throw std::runtime_error("worker failed");
                }
            });
    }
    catch (const std::runtime_error&)
    {
        return 0;
    }

    return 1;
}
```

## Related pages

- @ref test_support_expectations
- @ref test_support_files_environment
- @ref test_support_child_processes
- @ref test_support_timing_stress
