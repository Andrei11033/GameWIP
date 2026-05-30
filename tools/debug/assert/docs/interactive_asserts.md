@page assert_interactive Interactive asserts

Interactive assertions are developer/debug tools. They are separate from normal `ASSERT` and `VERIFY`, and they should be used only where continuing after a failure is acceptable during development.

## Actions

- **Break**: force-calls the debugger break path and continues if execution resumes.
- **Abort**: terminates the process with `std::abort()`.
- **Ignore Once**: continues this failure only. The next failure at the same call site can report again.
- **Always Ignore**: sets a per-call-site atomic flag and suppresses future failures from that macro expansion.

`Always Ignore` is local to the macro expansion site. It is not a global ignore list and it does not affect normal `ASSERT` or `VERIFY` macros.

## Side-effect behavior

`ASSERT_INTERACTIVE` is assertion-like: after a call site has been Always Ignored, the condition is skipped. Use it only when skipping the expression is acceptable.

`VERIFY_INTERACTIVE` is verification-like: the condition is always evaluated once, even when future reports from the same call site are Always Ignored.

## Automated tests

Automated interactive tests must not open real UI. They use `GAMEWIP_ASSERT_TEST_ACTION` with one of these values:

- `break`
- `abort`
- `ignore_once`
- `always_ignore`

`GAMEWIP_ASSERT_SUPPRESS_POPUP=1` suppresses interactive UI and makes the default action Abort unless `GAMEWIP_ASSERT_TEST_ACTION` supplies a valid action.

## Manual UI

Manual UI tests intentionally open real Windows dialogs and require user interaction. They must be gated by runtime options and should run near the end of the test suite.

On Windows, the preferred UI path is TaskDialog with the Common Controls v6 manifest. The MessageBox fallback is for degraded environments; it can represent Abort, Break, and Ignore Once, but not a true four-button Always Ignore UI.
