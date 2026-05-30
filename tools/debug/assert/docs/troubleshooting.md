@page assert_troubleshooting Assert troubleshooting

## A popup appears during automated tests

Automated interactive tests must use `GAMEWIP_ASSERT_TEST_ACTION` and should suppress real UI. Manual UI tests are runtime-gated and should run only when the user intentionally enables them.

## My expression is not evaluated

You probably used an assertion-like macro in a disabled build. `ASSERT`, `CHECK`, `CHECK_ONCE`, and `ASSERT_INTERACTIVE` may skip the condition when disabled. Use `VERIFY` or `ENSURE` if the expression must always run.

## My message code does not run

`_MSG` message expressions are diagnostic-only. They are not evaluated unless the diagnostic path is taken and diagnostics are enabled.

## Always Ignore did not suppress another assert

Always Ignore is per macro expansion site. It is not a global ignore registry and does not affect normal `ASSERT` / `VERIFY` macros.

## Break does not enter my debugger

Check whether a debugger is attached and whether the platform break instruction is supported in the current environment. Child-process tests validate break behavior separately from the main runner.

## MessageBox fallback has fewer actions

The TaskDialog path can represent the full interactive action set. The MessageBox fallback is degraded and cannot present a true four-button Always Ignore UI.

## Abort happens instead of Break

Without an attached debugger, the safe default action is Abort. In automated tests, use `GAMEWIP_ASSERT_TEST_ACTION=break` only for scenarios that are isolated and expected to handle the break path.

## CHECK_ONCE reports more than expected

`CHECK_ONCE` is per macro expansion site. Two identical-looking macro calls on different lines are separate sites. Concurrent failures at the same site can race on which thread performs the first report.

## Related pages

- @ref assert_macro_behavior
- @ref assert_interactive
- @ref assert_test_hooks
