@page assert_testing Assert maintainer validation

@note This page is for maintainers. Internal hooks and forced actions are source-tree validation interfaces, not consumer API.

## Macro coverage

The Assert suite covers enabled and disabled builds, expression evaluation count, message laziness, fatal versus recoverable families, `CHECK_ONCE`, `ENSURE` return values, diagnostics controls, source location, and Logger report integration.

Passing-path benchmarks verify that enabled successful macros avoid failure formatting/report work. Disabled-build tests verify that each macro family preserves its documented evaluation contract.

## Child and interactive coverage

Abort, unreachable, and debugger-break paths run in child processes so the parent suite can continue and assert exact results. Automated interactive tests use the internal forced-action definition and do not open dialogs.

Real Win32 UI is manual and runtime opt-in. It covers Ignore Once, Always Ignore, Break with a debugger, Abort in a child, primary-dialog fallback, and fallback default behavior.

## Hook coverage

With `INTERNAL_ASSERT_TEST_HOOKS=1`, tests can force primary action-dialog fallback, fallback behavior, debugger-attached results, and popup suppression. State is reset after every scenario.

GameWIP owns module registration, child routing, UI selection, benchmarks, reports, and coverage. See @ref project_testing, @ref project_benchmarking, and @ref project_coverage.
