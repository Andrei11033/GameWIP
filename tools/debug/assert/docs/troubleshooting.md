@page assert_troubleshooting Assert troubleshooting

## A popup appears during automated tests

Automated and manual interactive validation are described under @ref assert_testing. Normal application runs should enable interactive UI only when a developer is present to respond.

## The expression is not evaluated

An assertion-like macro was probably used in a disabled build. `ASSERT`, `CHECK`, `CHECK_ONCE`, and `ASSERT_INTERACTIVE` may skip the condition when disabled. Use `VERIFY` or `ENSURE` if the expression must always run.

## The message expression does not run

`_MSG` message expressions are diagnostic-only. They are not evaluated unless the diagnostic path is taken and diagnostics are enabled.

## Always Ignore did not suppress another assert

Always Ignore is per macro expansion site. It is not a global ignore registry and does not affect normal `ASSERT` / `VERIFY` macros.

## Break does not enter the debugger

Check whether a debugger is attached and whether the platform break instruction is supported in the current environment. Child-process tests validate break behavior separately from the main runner.

## The fallback dialog has fewer actions

The preferred dialog can represent the full interactive action set. The fallback dialog cannot present a true four-action Always Ignore interface.

## Abort happens instead of Break

Without an attached debugger, the safe default action is Abort. See @ref assert_testing for isolated validation of the Break path.

## CHECK_ONCE reports more than expected

`CHECK_ONCE` is per macro expansion site. Two identical-looking macro calls on different lines are separate sites. Concurrent failures at the same site can race on which thread performs the first report.

## Related pages

- @ref assert_macro_behavior
- @ref assert_interactive
- @ref assert_test_hooks
