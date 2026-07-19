@page assert_macros Macros

This page explains when to use each macro family. The authoritative side-effect matrix is @ref assert_macro_behavior.

## Fatal assertions

`ASSERT(condition)` validates a development invariant. When assertions are disabled, `condition` is not evaluated.

`ASSERT_MSG(condition, message)` follows the same contract and adds diagnostic text on the enabled failure path. The message expression is diagnostic-only.

Use these macros when continuing after failure would leave the process in an invalid state and the checked expression has no required side effects.

## Verification

`VERIFY(condition)` uses the fatal assertion path when enabled, but still evaluates `condition` when assertion reporting is disabled.

`VERIFY_MSG(condition, message)` adds lazy diagnostic message text to the enabled failure path.

Use these macros for calls that must happen in every build, such as cleanup, commit, or validation operations with side effects.

## Interactive assertions

`ASSERT_INTERACTIVE(condition)` and `ASSERT_INTERACTIVE_MSG(condition, message)` use the interactive failure path when assertions are enabled. `AlwaysIgnore` suppresses future failures from the same macro expansion site and may skip later `ASSERT_INTERACTIVE` condition evaluation at that site.

`VERIFY_INTERACTIVE(condition)` and `VERIFY_INTERACTIVE_MSG(condition, message)` always evaluate the condition once, including after `AlwaysIgnore` suppresses reporting for that call site.

Interactive assertions are developer tools. Use `CHECK` or normal control flow for recoverable runtime errors.

## Recoverable checks

`CHECK(condition)` reports a recoverable failure and continues when checks are enabled. Disabled checks do not evaluate the condition.

`CHECK_MSG(condition, message)` adds lazy diagnostic message text.

`CHECK_ONCE(condition)` and `CHECK_ONCE_MSG(condition, message)` report only the first failed reporting attempt from one macro expansion site. The suppression flag is local to that expansion site, not to the expression text globally.

## Boolean checks

`ENSURE(condition)` evaluates `condition` once and returns the boolean result. When checks are enabled and the result is false, it also reports a recoverable failure.

`ENSURE_MSG(condition, message)` adds lazy diagnostic message text to the reported false path.

Use `ENSURE` when the caller needs a value for control flow and the expression must run even when recoverable reporting is disabled.

## Unreachable and debug break

`UNREACHABLE()` marks an impossible control-flow path. Enabled assertions use the fatal failure path; disabled assertion builds use the configured trap or compiler unreachable hint.

`DEBUG_BREAK()` explicitly triggers the platform debug-break path. It is independent of `ASSERT_ENABLED` and `ASSERT_CHECKS_ENABLED`.

## Related pages

- @ref assert_macro_behavior
- @ref assert_failure_actions
- @ref assert_interactive
- @ref assert_diagnostics
