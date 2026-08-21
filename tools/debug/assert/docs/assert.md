@page assert Assert

The Assert library provides fatal assertions, recoverable checks, explicit debug breaks, and optional interactive developer failure actions.

Assert reports through Logger. Fatal paths use Logger's synchronous report path so diagnostics are written before debugger, popup, or termination handling continues.

## How the library is organized

The macro families differ along two axes: whether failure is fatal and whether
the checked expression still runs when that family is compiled out. Runtime
support builds one diagnostic record, submits it synchronously, and then applies
the selected failure action. Build options decide whether fatal assertions,
recoverable checks, diagnostics, and interactive handling exist in a given
configuration; those choices are exported to consumers through the CMake target.

## Consumer manual

- @subpage assert_quick_start — Include, link, choose a macro family, and
  configure a minimal consumer.
- @subpage assert_public_api — Find macros, runtime functions, compile-time
  settings, and the package boundary.
- @subpage assert_configuration — Understand `AUTO`, forced states,
  diagnostics, interactive support, manifests, and exported definitions.
- @subpage assert_abi — Understand interface-only versus shared-runtime builds,
  exports, package identity, and supported configuration matching.
- @subpage assert_macros — Choose the right fatal, recoverable, result-returning,
  or unreachable macro.
- @subpage assert_macro_behavior — Check exact evaluation, reporting, return,
  termination, and disabled-build behavior.
- @subpage assert_diagnostics — Understand captured text, source information,
  UTF-8 handling, and diagnostics-disabled builds.
- @subpage assert_failure_actions — Follow report delivery, debugger behavior,
  termination, and forced actions.
- @subpage assert_interactive — Understand developer prompts, Ignore Once,
  Always Ignore, synchronization, and noninteractive fallbacks.
- @subpage assert_examples — See each macro family and configuration in context.
- @subpage assert_troubleshooting — Diagnose missing evaluation, unexpected
  termination, debugger traps, dialogs, manifests, and configuration mismatch.

## Maintainer validation

- @subpage assert_testing — See the configuration matrix and automated runtime,
  package, ABI, and subprocess coverage.
- @subpage assert_test_hooks — Understand source-tree-only forced actions,
  diagnostic capture, and reset rules.

## Generated API reference

Use @ref GameWIP::Debug::Assert for the runtime namespace reference. The global
macro reference is generated from `debug/assert/assert.h`. For choosing a macro
and understanding exactly when its expressions run, read @ref assert_macros and
@ref assert_macro_behavior.

## Key behavior

`ASSERT` is for fatal invariants whose expression may disappear in disabled builds. `VERIFY` is for fatal checks whose expression must always run. `CHECK` and `CHECK_ONCE` report recoverable failures. `ENSURE` evaluates once, reports false results when checks are enabled, and returns the boolean result. Interactive assertions add Break, Abort, Ignore Once, and Always Ignore choices for developer workflows.

## Dependency boundary

The public C++ include is `debug/assert/assert.h`. The installed CMake target is `GameWIP::Assert`; source-tree targets may link `Assert`.

When failure-reporting families can be enabled, Assert builds a shared runtime
and uses Logger privately for diagnostic delivery. With both families forced
off, it becomes an interface-only target and has no runtime dependency.

Assert owns assertion policy and platform failure presentation. Logger owns log formatting, sinks, queueing, and report delivery. Engine runtime policy, game recovery decisions, validation runner orchestration, and platform backend implementation details belong outside the Assert public manual unless they affect the supported package or macro contract.
