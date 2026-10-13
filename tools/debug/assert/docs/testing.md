@page assert_testing Testing

@note This page describes validation coverage. Platform-specific details and forced actions belong to source-tree validation, not consumer API.

## Macro coverage

The enabled Assert suite and disabled installed consumer cover expression evaluation count, message laziness, fatal versus recoverable
families, `CHECK_ONCE`,
`ENSURE` return values, diagnostics controls, source location, Logger report integration, UTF-8-safe bounded diagnostic truncation, and package-facing
compile definitions.

The suite is one logical Assert module. Private behavior cases are grouped into focused `.inl` fragments for macro behavior, diagnostics,
interactive handling, stress, process/crash handling, and manual UI validation. Passing-path benchmarks verify that enabled successful macros avoid
failure formatting and reporting work. The interface-only disabled consumer executes every disabled macro family's evaluation contract.
Configuration-specific cases that do not execute in a selected build remain skips.

## Retained source-tree validation seam

`ASSERT_ENABLE_TEST_HOOKS` enables the narrow source-tree validation seam. `ASSERT_INTERNAL_TEST_HOOKS` is build-tree-only and must not leak into
installed consumers. The retained adapter validates diagnostic-preparation failure inside the outer `noexcept` popup boundary: it calls the real
`Platform::showErrorPopup()` path, suppresses only final native presentation for the current test thread, and reports whether the armed one-shot was
consumed. This interface is not installed and is not consumer API.

## Child and interactive coverage

Abort, unreachable, and debugger-break paths run in child processes so the parent suite can continue and assert exact results. Automated interactive
tests use deterministic environment paths and do not open real dialogs.

Disabled or unavailable scenarios count as skips; compiled-out macro evaluation remains an actual behavioral test.

Real Win32 UI is manual and runtime opt-in. It asks for operator confirmation of Ignore Once, suppression after Always Ignore, Break with a debugger,
and Abort in a child. Returning from a call alone does not count as a successful UI observation. Use `s reason` for a skip; end-of-input
keeps its own reason. The manual Abort child allows five minutes for an operator response; automated child probes retain their 30-second budget.
Environment controls provide deterministic interactive action paths, while the retained source-tree seam above exercises the noexcept
diagnostic-preparation emergency path
without opening real UI.

GameWIP owns module registration, child routing, UI selection, benchmarks, reports, and coverage. See @ref project_testing, @ref project_benchmarking,
and @ref project_coverage.

Installed-package validation includes a dedicated interface-only Assert configuration with both assertion families disabled. Its executable verifies
that ASSERT/CHECK families skip condition and message evaluation, VERIFY evaluates exactly once, and ENSURE preserves true/false results
with lazy messages. The normal runtime
package is also consumed with Assert and its dependencies in separate prefixes. Consumer executables select application manifest requirements through
the shared helper described in @ref assert_configuration.

Manual runs report prompted observations separately from setup and contract assertions.

## Related pages

- @ref assert_macro_behavior
- @ref assert_failure_actions
