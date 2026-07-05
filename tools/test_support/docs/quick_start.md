@page test_support_quick_start TestSupport quick start

## Include

Include the library's public header:

```cpp
#include "test_support/test_support.h"
```

## Installed CMake

Use the package's namespaced imported target:

```cmake
find_package(TestSupport CONFIG REQUIRED)
target_link_libraries(MyTests PRIVATE GameWIP::TestSupport)
```

## Source-tree CMake

When TestSupport is part of the same source tree, use its short build target:

```cmake
target_link_libraries(MyTests PRIVATE TestSupport)
```

TestSupport has no dependencies on other project libraries, so it can support any test executable.

## Minimal usage

The normal test-executable flow is:

1. Create `GameWIP::TestSupport::Types::ReportOptions`.
2. Create a `GameWIP::TestSupport::Runner`.
3. Run named suites.
4. Use `GameWIP::TestSupport::Context` inside each suite.
5. Return `runner.exitCode()` from `main`.

Example:

```cpp
#include "test_support/test_support.h"

int main()
{
    GameWIP::TestSupport::Types::ReportOptions options;
    options.reportPath = "logs/tests/latest_test_report.txt";

    GameWIP::TestSupport::Runner runner(options);

    runner.runSuite(
        "Math",
        [](GameWIP::TestSupport::Context& context)
        {
            context.expectEq("one plus one", 2, 1 + 1);
            context.expectNear("fraction", 0.5, 1.0 / 2.0, 0.0001);
        });

    return runner.exitCode();
}
```

Expectations return `true` when they pass and `false` when they fail. They also record the outcome in the context, so a failing expectation does not abort the suite.

## Where to go next

- @ref test_support_expectations explains `Runner`, `Context`, expectations, summaries, and sections.
- @ref test_support_reports explains console/report-file behavior and report categories.
- @ref test_support_files_environment explains file helpers and scoped environment variables.
- @ref test_support_child_processes explains isolated child-process tests.
- @ref test_support_timing_stress explains timers, metrics, start gates, stop flags, and workers.
