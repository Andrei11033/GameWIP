@page test_support_manual_tests TestSupport manual checks

Manual checks use `Types::ManualAnswer` and `promptManualCheck()`.

Manual checks are for behavior that code cannot verify on its own, such as visual Windows UI behavior or debugger interaction. They must be gated by runtime options and should run near the end of a test run.

Automated tests should not depend on clicking dialogs.

The project test executable exposes a focused TestSupport manual run:

```powershell
.\build\GameWIP.exe --test-support-manual
```

The regular automated TestSupport run stays non-blocking:

```powershell
.\build\GameWIP.exe --test-support-only
```

Example:

```cpp
if (manualTestsEnabled)
{
    const auto answer = GameWIP::TestSupport::promptManualCheck(
        "Did the popup show the expected four buttons?");

    if (answer == GameWIP::TestSupport::Types::ManualAnswer::Yes)
    {
        context.pass("popup buttons visible");
    }
    else if (answer == GameWIP::TestSupport::Types::ManualAnswer::No)
    {
        context.fail("popup buttons visible", "user reported incorrect UI");
    }
    else
    {
        context.skip("popup buttons visible", "manual check skipped");
    }
}
```
