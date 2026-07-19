@page test_support_quick_start Quick start

## Include

```cpp
#include "test_support/test_support.h"
```

## Installed CMake

Set `GAMEWIP_REQUIRED_VERSION` from the consuming project's dependency lock; see @ref project_library_compatibility.

```cmake
find_package(TestSupport ${GAMEWIP_REQUIRED_VERSION} EXACT CONFIG REQUIRED)
target_link_libraries(MyTests PRIVATE GameWIP::TestSupport)
```

## Source-tree CMake

```cmake
target_link_libraries(MyTests PRIVATE TestSupport)
```

TestSupport has no dependency on another project library.

## Minimal usage

```cpp
#include "test_support/test_support.h"

int main()
{
    namespace TS = GameWIP::TestSupport;

    TS::Types::ReportOptions options;
    options.reportPath = "logs/tests/latest_test_report.txt";

    TS::Runner runner(options);
    runner.runSuite(
        "Math",
        [](TS::Context& context)
        {
            static_cast<void>(context.expectEq("one plus one", 2, 1 + 1));
            static_cast<void>(
                context.expectNear("one half", 0.5, 1.0 / 2.0, 0.0001));
        });

    return runner.exitCode();
}
```

A failed expectation records a failure and returns `false`; it does not stop the suite. Use the returned value when later work depends on the check:

```cpp
#include "test_support/test_support.h"

void validateFixture(
    GameWIP::TestSupport::Context& context,
    bool fixtureLoaded)
{
    if (!context.expectTrue("fixture loaded", fixtureLoaded))
    {
        return;
    }

    context.info("fixture-dependent checks can now run");
}
```

## Failure handling

- `Runner::runSuite()` catches exceptions thrown by the suite callable and records one failed check named `uncaught exception`.
- Reporting and report-file operations do not determine the test result. A report-file open, write, or flush failure disables that sink and emits at most one stderr diagnostic.
- Formatting, allocation, path conversion, filesystem setup, standard-stream, and thread-creation failures can still throw from APIs that are not marked `noexcept`.
- `runChildProcess()` preserves the native `std::uint32_t` exit code and reports launch, wait, inspection, or capture failure separately through `infrastructureFailure`. Inspect that flag, `timedOut`, and `wasTerminatedByTest` before interpreting the exit code.
- `runner.exitCode()` reflects recorded failures only. A skipped-only or empty run returns zero.

## Where to go next

- @ref test_support_public_api maps every public type and operation family.
- @ref test_support_expectations explains runners, contexts, expectations, and sections.
- @ref test_support_reports explains console and report-file behavior.
- @ref test_support_files_environment explains fixture helpers and process-global guards.
- @ref test_support_child_processes explains launch, capture, timeout, and result interpretation.
- @ref test_support_timing_stress explains timers and worker coordination.
- @ref test_support_examples provides complete integration examples.
