@page assert_macros Assert macros

## Fatal macros

- `ASSERT` evaluates only when assertions are enabled. On failure it reports Fatal and aborts.
- `ASSERT_MSG` adds a diagnostic message expression.
- `VERIFY` always evaluates its expression. It reports and aborts only when assertions are enabled and the expression is false.
- `VERIFY_MSG` always evaluates its condition and adds a diagnostic message only on the failure path.
- `UNREACHABLE` marks code that should not execute.
- `DEBUG_BREAK` explicitly triggers the platform debug-break path.

## Recoverable macros

- `CHECK` reports an Error and continues when checks are enabled.
- `CHECK_MSG` adds a diagnostic message expression.
- `CHECK_ONCE` reports only the first failure from one macro call site.
- `ENSURE` evaluates once, returns the boolean result, and reports when checks are enabled and the result is false.
- `ENSURE_MSG` adds a diagnostic message only when reporting.

## Interactive macros

- `ASSERT_INTERACTIVE` uses the interactive action path when assertions are enabled.
- `ASSERT_INTERACTIVE_MSG` adds a diagnostic message expression.
- `VERIFY_INTERACTIVE` always evaluates its condition and uses the interactive action path only when enabled and false.
- `VERIFY_INTERACTIVE_MSG` combines always-evaluated condition behavior with lazy message diagnostics.

## Diagnostics

When diagnostics are enabled, failures include condition text, optional message, file, line, and caller function where available.

See @ref assert_macro_behavior for the full side-effect table.
