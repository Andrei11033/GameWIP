@page assert Assert

The GameWIP Assert library provides fatal assertions, recoverable checks, and optional interactive developer failure actions.

The assert library reports through the Logger library. Fatal assertion paths use synchronous logger reports so failure diagnostics are written immediately.

## User manual

- @subpage assert_quick_start
- @subpage assert_public_api
- @subpage assert_macros
- @subpage assert_macro_behavior
- @subpage assert_diagnostics
- @subpage assert_interactive
- @subpage assert_failure_actions
- @subpage assert_troubleshooting
- @subpage assert_examples

## Developer validation references

These pages are for maintainers validating the library, not for normal assert use:

- @subpage assert_testing
- @subpage assert_test_hooks

## Key behavior

`ASSERT` is for fatal invariants. `VERIFY` is for fatal checks whose expression must always run. `CHECK` and `CHECK_ONCE` report recoverable failures. `ENSURE` evaluates exactly once, reports when enabled and false, and returns the boolean result. Interactive asserts are developer-only failure paths with Break, Abort, Ignore Once, and Always Ignore choices.
