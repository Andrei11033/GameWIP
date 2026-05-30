@page assert_macros Assert macros

## Fatal macros

- `ASSERT(condition)`: evaluates `condition` only when assertions are enabled. A false result reports Fatal, may show the assert-owned popup, breaks only when a debugger is attached, then aborts.
- `ASSERT_MSG(condition, message)`: same as `ASSERT`, with a custom message evaluated only on failure and only when diagnostics are enabled.
- `VERIFY(condition)`: always evaluates `condition`. It reports/aborts only when assertions are enabled and the result is false.
- `VERIFY_MSG(condition, message)`: same as `VERIFY`, with a diagnostic message evaluated only when needed.
- `UNREACHABLE()`: marks impossible control flow. Enabled builds use the fatal assert path; disabled builds use the configured trap or compiler unreachable hint.
- `DEBUG_BREAK()`: force-enters the debugger/trap path without first checking whether a debugger is attached.

## Recoverable macros

- `CHECK(condition)`: evaluates only when checks are enabled. A false result reports Error and continues.
- `CHECK_MSG(condition, message)`: same as `CHECK`, with a custom message evaluated only on failure and only when diagnostics are enabled.
- `CHECK_ONCE(condition)`: reports only the first failure attempt from one macro call site. The expression is still evaluated when checks are enabled.
- `CHECK_ONCE_MSG(condition, message)`: same as `CHECK_ONCE`, with a custom message for the first report.
- `ENSURE(condition)`: always evaluates once, returns the boolean result, and reports false results only when checks are enabled.
- `ENSURE_MSG(condition, message)`: same as `ENSURE`, with a custom message evaluated only for reportable false results.

## Interactive macros

- `ASSERT_INTERACTIVE(condition)`: assertion-enabled builds evaluate the condition until the call site is Always Ignored. A false result reports Fatal and asks for Break / Abort / Ignore Once / Always Ignore.
- `ASSERT_INTERACTIVE_MSG(condition, message)`: same as `ASSERT_INTERACTIVE`, with a diagnostic message.
- `VERIFY_INTERACTIVE(condition)`: always evaluates the condition once. False results enter the interactive path when assertions are enabled unless the call site is Always Ignored.
- `VERIFY_INTERACTIVE_MSG(condition, message)`: same as `VERIFY_INTERACTIVE`, with a diagnostic message.

## Diagnostics

`GAMEWIP_ASSERT_DIAGNOSTICS` controls condition text, message text, file, line, and function capture. When diagnostics are off, custom message expressions are intentionally not evaluated.

## Disabled-mode side effects

- Disabled `ASSERT`, `ASSERT_MSG`, `ASSERT_INTERACTIVE`, `ASSERT_INTERACTIVE_MSG`, `CHECK`, `CHECK_MSG`, `CHECK_ONCE`, and `CHECK_ONCE_MSG` do not evaluate their condition.
- `VERIFY`, `VERIFY_INTERACTIVE`, `ENSURE`, and their `_MSG` variants still evaluate the condition once.
- `_MSG` message arguments are not evaluated when their diagnostic path is disabled or not taken.
