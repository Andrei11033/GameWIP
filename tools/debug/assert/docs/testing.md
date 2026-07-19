@page assert_testing Maintainer validation

@note This page is for maintainers. Internal hooks and forced actions are source-tree validation interfaces, not consumer API.

## Macro coverage

The Assert suite covers enabled and disabled builds, expression evaluation count, message laziness, fatal versus recoverable families, `CHECK_ONCE`, `ENSURE` return values, diagnostics controls, source location, Logger report integration, and package-facing compile definitions.

Passing-path benchmarks verify that enabled successful macros avoid failure formatting and reporting work. Disabled-build tests verify that each macro family preserves its documented evaluation contract.

## Child and interactive coverage

Abort, unreachable, and debugger-break paths run in child processes so the parent suite can continue and assert exact results. Automated interactive tests use deterministic hook or environment paths and do not open real dialogs.

Real Win32 UI is manual and runtime opt-in. It covers Ignore Once, Always Ignore, Break with a debugger, Abort in a child, primary-dialog fallback, and fallback default behavior.

## Hook coverage

With `INTERNAL_ASSERT_TEST_HOOKS=1`, tests can force primary action-dialog fallback, fallback behavior, debugger-attached results, and popup suppression. State must be reset after every scenario.

GameWIP owns module registration, child routing, UI selection, benchmarks, reports, and coverage. See @ref project_testing, @ref project_benchmarking, and @ref project_coverage.

Installed-package validation includes a dedicated interface-only Assert configuration with both assertion families disabled and a custom absolute data directory. The normal runtime package is also consumed with Assert and Logger in separate prefixes.

## Related pages

- @ref assert_test_hooks
- @ref assert_macro_behavior
- @ref assert_failure_actions
