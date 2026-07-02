@page assert Assert

The Assert library provides fatal assertions, recoverable checks, and optional interactive developer failure actions.

The assert library reports through the Logger library. Fatal assertion paths use synchronous logger reports so failure diagnostics are written immediately.

## User manual

- @subpage assert_quick_start
- @subpage assert_public_api
- @subpage assert_macros
- @subpage assert_macro_behavior
- @subpage assert_diagnostics
- @subpage assert_failure_actions
- @subpage assert_interactive
- @subpage assert_examples
- @subpage assert_troubleshooting

## Maintainer validation

- @subpage assert_testing
- @subpage assert_test_hooks

## Generated API reference

Use @ref GameWIP::Debug::Assert for runtime actions and support functions. Global macro reference is generated from `debug/assert/assert.h`; @ref assert_macros and @ref assert_macro_behavior provide the workflow and evaluation matrix for every macro family.

## Key behavior

`ASSERT` is for fatal invariants. `VERIFY` is for fatal checks whose expression must always run. `CHECK` and `CHECK_ONCE` report recoverable failures. `ENSURE` evaluates exactly once, reports when enabled and false, and returns the boolean result. Interactive asserts are developer-only failure paths with Break, Abort, Ignore Once, and Always Ignore choices.
