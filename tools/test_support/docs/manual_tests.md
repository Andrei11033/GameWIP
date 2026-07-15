@page test_support_manual_tests TestSupport manual checks

Manual checks cover behavior that automated code cannot reliably verify, such as visual UI, audible/terminal effects, or debugger interaction. They should be runtime-gated and normally run after automated scenarios.

`promptManualCheck()` writes:

```text
[MANUAL] <question> [y/n/s]:
```

It reads complete lines from stdin and repeats until it recognizes one of these exact variants:

- yes: `y`, `Y`, `yes`, `Yes`;
- no: `n`, `N`, `no`, `No`;
- skip: `s`, `S`, `skip`, `Skip`.

Surrounding whitespace is not trimmed. End-of-input returns `Types::ManualAnswer::Skipped`. The function does not clear or repair stream state after EOF/failure, can block indefinitely, and can propagate standard-stream exceptions when they are enabled.

```cpp
#include "test_support/test_support.h"

int main()
{
    namespace TS = GameWIP::TestSupport;

    TS::Types::ReportOptions options;
    options.writeReport = false;
    TS::Context context("Manual UI", options);

    switch (TS::promptManualCheck("Did the popup show the expected buttons?"))
    {
    case TS::Types::ManualAnswer::Yes:
        context.pass("popup buttons visible");
        break;
    case TS::Types::ManualAnswer::No:
        context.fail("popup buttons visible", "user reported incorrect UI");
        break;
    case TS::Types::ManualAnswer::Skipped:
        context.skip("popup buttons visible", "manual check skipped");
        break;
    }

    return context.ok() ? 0 : 1;
}
```

Automated validation must not depend on a person interacting with dialogs or stdin. The application owns how manual mode is selected and whether a skipped manual check is acceptable.

## Related pages

- @ref test_support_expectations
- @ref test_support_reports
