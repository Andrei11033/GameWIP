@page assert_testing Maintainer validation

@note This page is for maintainers. Platform-specific details and forced actions are source-tree validation concerns, not consumer API.

## Macro coverage

The Assert suite covers enabled and disabled builds, expression evaluation count, message laziness, fatal versus recoverable families, `CHECK_ONCE`,
`ENSURE` return values, diagnostics controls, source location, Logger report integration, UTF-8-safe bounded diagnostic truncation, and package-facing
compile definitions.

The suite is one logical Assert module. Private behavior cases are grouped into focused `.inl` fragments for macro behavior, diagnostics,
interactive handling, stress, process/crash handling, and manual UI validation. Passing-path benchmarks verify that enabled successful macros avoid
failure formatting and reporting work. Disabled-build tests verify that each macro family preserves its documented evaluation contract.

## Retained source-tree validation seam

`ASSERT_ENABLE_TEST_HOOKS` enables the narrow source-tree validation seam. `ASSERT_INTERNAL_TEST_HOOKS` is build-tree-only and must not leak into
installed consumers. The retained adapter validates diagnostic-preparation failure inside the outer `noexcept` popup boundary: it calls the real
`Platform::showErrorPopup()` path, suppresses only final native presentation for the current test thread, and reports whether the armed one-shot was
consumed. This interface is not installed and is not consumer API.

## Child and interactive coverage

Abort, unreachable, and debugger-break paths run in child processes so the parent suite can continue and assert exact results. Automated interactive
tests use deterministic environment paths and do not open real dialogs.

Real Win32 UI is manual and runtime opt-in. It covers Ignore Once, Always Ignore, Break with a debugger, and Abort in a child. Environment controls
provide deterministic interactive action paths, while the retained source-tree seam above exercises the noexcept diagnostic-preparation emergency path
without opening real UI.

GameWIP owns module registration, child routing, UI selection, benchmarks, reports, and coverage. See @ref project_testing, @ref project_benchmarking,
and @ref project_coverage.

Installed-package validation includes a dedicated interface-only Assert configuration with both assertion families disabled. The normal runtime
package is also consumed with Assert and its dependencies in separate prefixes. Consumer executables select application manifest requirements through
the shared helper described in @ref assert_configuration.

## Related pages

- @ref assert_macro_behavior
- @ref assert_failure_actions
