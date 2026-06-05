@page assert Assert

The GameWIP Assert library provides fatal assertions, recoverable checks, and optional interactive developer failure actions.

The assert library reports through the Logger library. Fatal assertion paths use synchronous logger reports so failure diagnostics are written immediately.

## Documentation sections

### User manual

- @subpage assert_getting_started
- @subpage assert_quick_start
- @subpage assert_public_api
- @subpage assert_examples
- @subpage assert_troubleshooting
- @subpage assert_macros
- @subpage assert_macro_behavior_overview
- @subpage assert_diagnostics
- @subpage assert_failure_actions
- @subpage assert_interactive

### Reference and concepts

- @subpage assert_api_reference

### Developer validation

- @subpage assert_testing
- @subpage assert_test_hooks
- @subpage assert_developer_validation

## Normal user path

Use the getting started and API reference pages for public integration. Macro behavior pages define enabled/disabled expression evaluation, diagnostics, fatal behavior, recoverable checks, and interactive developer actions. Developer validation pages are for maintainers and rare-path testing.

## Key behavior

`ASSERT` is for fatal invariants. `VERIFY` is for fatal checks whose expression must always run. `CHECK` and `CHECK_ONCE` report recoverable failures. `ENSURE` evaluates exactly once, reports when enabled and false, and returns the boolean result. Interactive asserts are developer-only failure paths with Break, Abort, Ignore Once, and Always Ignore choices.
