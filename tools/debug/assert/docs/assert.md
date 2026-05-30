@page assert Assert

The GameWIP Assert library provides fatal assertions, recoverable checks, exactly-once boolean validation helpers, explicit debug breaks, unreachable-code markers, and optional interactive developer failure actions.

The assert runtime reports through the Logger library. Fatal paths use synchronous logger reports so failure diagnostics are written immediately instead of waiting behind normal async log traffic.

## Macro families

- **Fatal**: `ASSERT`, `ASSERT_MSG`, `VERIFY`, `VERIFY_MSG`, `UNREACHABLE`.
- **Interactive fatal**: `ASSERT_INTERACTIVE`, `ASSERT_INTERACTIVE_MSG`, `VERIFY_INTERACTIVE`, `VERIFY_INTERACTIVE_MSG`.
- **Recoverable**: `CHECK`, `CHECK_MSG`, `CHECK_ONCE`, `CHECK_ONCE_MSG`, `ENSURE`, `ENSURE_MSG`.
- **Debugger**: `DEBUG_BREAK`.

## Guide pages

- @ref assert_quick_start
- @ref assert_macros
- @ref assert_interactive
- @ref assert_testing
- @ref assert_examples

## API reference

Most of the public surface is macro-based and documented in `debug/assert/assert.h`. `GameWIP::Debug::Assert::FailureAction` documents the interactive action enum.
