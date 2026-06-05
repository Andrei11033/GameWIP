@page assert_interactive Interactive asserts

Interactive assertions are separate from normal `ASSERT` and `VERIFY`.

They are intended for developer/debug workflows where the developer may choose what happens after a failure.

Available actions:

- **Break**: trigger the debugger break path and continue if the debugger resumes.
- **Abort**: terminate the process.
- **Ignore Once**: continue this time only.
- **Always Ignore**: suppress future failures from the same macro call site.

`AlwaysIgnore` is local to the macro expansion site. It is not a global ignore list.

## Dialog behavior

On Windows, Assert prefers a TaskDialog path when available so the full action set can be presented. If TaskDialog is unavailable or forced to fail in tests, Assert falls back to MessageBox behavior with a reduced action mapping.

When a debugger is attached, the default action favors Break. Without a debugger, the safe default is Abort unless `GAMEWIP_ASSERT_TEST_ACTION` provides a valid test action.

## Test behavior

Automated tests use `GAMEWIP_ASSERT_TEST_ACTION` to exercise interactive behavior without opening real UI. Manual UI tests intentionally open real Windows dialogs and require user interaction. They must only run when runtime `TestRunOptions` request them.
