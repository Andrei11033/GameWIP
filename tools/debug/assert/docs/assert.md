@page assert Assert

The GameWIP Assert library provides fatal assertions, recoverable checks, and optional interactive developer failure actions.

The assert library reports through the Logger library. Fatal assertion paths use synchronous logger reports so failure diagnostics are written immediately.

## Documentation sections

- @subpage assert_getting_started
- @subpage assert_api_reference
- @subpage assert_macro_behavior_group
- @subpage assert_developer_validation

## Normal user path

Most users should read Assert getting started first, then Assert API reference. Macro behavior pages are the source of truth for enabled/disabled expression evaluation, diagnostics, fatal behavior, recoverable checks, and interactive developer actions. Developer validation pages are generated for maintainers and rare-path testing.

## Key behavior

`ASSERT` is for fatal invariants. `VERIFY` is for fatal checks whose expression must always run. `CHECK` and `CHECK_ONCE` report recoverable failures. `ENSURE` evaluates exactly once, reports when enabled and false, and returns the boolean result. Interactive asserts are developer-only failure paths with Break, Abort, Ignore Once, and Always Ignore choices.
