@page assert Assert

The Assert library provides fatal assertions, recoverable checks, explicit debug breaks, and optional interactive developer failure actions.

Assert reports through Logger. Fatal paths use Logger's synchronous report path so diagnostics are written before debugger, popup, or termination handling continues.

## Consumer manual

- @subpage assert_quick_start
- @subpage assert_public_api
- @subpage assert_configuration
- @subpage assert_abi
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

Use @ref GameWIP::Debug::Assert for the runtime namespace reference. Global macro reference is generated from `debug/assert/assert.h`; @ref assert_macros and @ref assert_macro_behavior are the manual owners for macro usage and expression-evaluation behavior.

## Key behavior

`ASSERT` is for fatal invariants whose expression may disappear in disabled builds. `VERIFY` is for fatal checks whose expression must always run. `CHECK` and `CHECK_ONCE` report recoverable failures. `ENSURE` evaluates once, reports false results when checks are enabled, and returns the boolean result. Interactive assertions add Break, Abort, Ignore Once, and Always Ignore choices for developer workflows.

## Dependency boundary

The public C++ include is `debug/assert/assert.h`. The installed CMake target is `GameWIP::Assert`; source-tree targets may link `Assert`.

Assert owns assertion policy and platform failure presentation. Logger owns log formatting, sinks, queueing, and report delivery. Engine runtime policy, game recovery decisions, validation runner orchestration, and platform backend implementation details belong outside the Assert public manual unless they affect the supported package or macro contract.
