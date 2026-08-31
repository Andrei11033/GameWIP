@page test_support_quick_start Quick start

This path creates a runner, records expectations in a suite, and emits the
result. It is the smallest complete validation program built on TestSupport.

## Include

The normal umbrella is:

```cpp
#include "test_support/test_support.h"
```

Focused entry headers are `test_support/types.h`, `reporting.h`, `files.h`,
`process.h`, and `stress.h`.

## Installed CMake

Set `GAMEWIP_REQUIRED_VERSION` from the consuming project's dependency lock;
see @ref project_library_compatibility.

```cmake
find_package(TestSupport ${GAMEWIP_REQUIRED_VERSION} EXACT CONFIG REQUIRED)
target_link_libraries(MyTests PRIVATE GameWIP::TestSupport)
```

The package resolves its exact matching Unicode implementation dependency.

## Source-tree CMake

```cmake
target_link_libraries(MyTests PRIVATE TestSupport)
```

## Minimal usage

```cpp
#include "test_support/test_support.h"

int main()
{
    namespace TS = GameWIP::TestSupport;

    TS::Types::Reporting::Options options;
    options.reportPath = "logs/validation/latest_test_report.txt";

    TS::Runner runner(options);
    runner.runSuite(
        "Math",
        [](TS::Context &context)
        {
            static_cast<void>(context.expectEq("one plus one", 2, 1 + 1));
        });

    return runner.exitCode();
}
```

An expectation records one result and returns a boolean; it does not abort the
suite. `Runner` converts an exception escaping a suite callback into a failed
check and keeps later suites runnable.

## Failure handling

TestSupport separates infrastructure status from domain outcomes. Inspect a
returned `status` before reading its text, boolean, count, path, or child-process
payload. A successfully launched child may still exit nonzero or time out; that
domain outcome is not an infrastructure launch failure.

Text-file helpers accept and return strict UTF-8. Child `outputBytes` remains
arbitrary captured data unless the child protocol independently guarantees an
encoding. Filesystem, current-directory, and environment guards restore state
on a best-effort basis and require caller coordination around process-global
state.

## Where to go next

- @ref test_support_public_api inventories the public surface and result model.
- @ref test_support_expectations explains contexts, runners, and expectations.
- @ref test_support_files_environment defines strict text and process-global guards.
- @ref test_support_child_processes defines launch, capture, timeout, and cleanup.
- @ref test_support_examples provides focused fixtures, manual checks, and stress examples.
- @ref test_support_troubleshooting maps common failures to their owning contract.
