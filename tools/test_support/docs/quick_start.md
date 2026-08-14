@page test_support_quick_start Quick start

The normal include remains:

```cpp
#include "test_support/test_support.h"
```

Focused entry headers are available for narrower consumers:

```cpp
#include "test_support/types.h"
#include "test_support/reporting.h"
#include "test_support/files.h"
#include "test_support/process.h"
#include "test_support/stress.h"
```

Installed CMake:

```cmake
find_package(TestSupport ${GAMEWIP_REQUIRED_VERSION} EXACT CONFIG REQUIRED)
target_link_libraries(MyTests PRIVATE GameWIP::TestSupport)
```

The package resolves the exact matching foundational Unicode dependency automatically.

## Minimal reporting example

```cpp
namespace TS = GameWIP::TestSupport;

TS::Types::Reporting::Options options;
options.reportPath = "logs/tests/latest_test_report.txt";

TS::Runner runner(options);
runner.runSuite(
    "Math",
    [](TS::Context& context)
    {
        static_cast<void>(context.expectEq("one plus one", 2, 1 + 1));
    });

return runner.exitCode();
```

## Child process example

```cpp
TS::Types::Process::Options child;
child.executablePath = "helper.exe";
child.arguments = {"--probe"};

const TS::Types::Process::Result result = TS::runChildProcess(child);
if (!result.status.ok())
    return 1;
if (result.outcome == TS::Types::Process::Outcome::Exited)
    return result.exitCode == 0 ? 0 : 1;
```

`outputBytes` is arbitrary captured stdout/stderr data. Do not treat it as UTF-8 unless the child protocol separately guarantees that.

## Text files

`readTextFile()` and `writeTextFile()` are strict UTF-8 helpers. Use them for test reports and textual fixtures. Malformed input returns `InfrastructureError::EncodingFailed`; malformed writes are rejected before destructive filesystem effects.

@ref test_support_public_api
@ref test_support_files_environment
@ref test_support_child_processes
