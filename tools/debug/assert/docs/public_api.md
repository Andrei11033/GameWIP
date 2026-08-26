@page assert_public_api Public API

Assert's public C++ surface is the installed header `debug/assert/assert.h`. The macros are global by design; runtime support lives in
`GameWIP::Debug::Assert`.

## Public macro families

| Family | APIs | Use for | Failure behavior |
| --- | --- | --- | --- |
| Fatal assertions | `ASSERT`, `ASSERT_MSG` | Development invariants whose expression may be omitted in disabled builds. | Logger Fatal report, optional popup, debugger break when attached, then abort. |
| Verification | `VERIFY`, `VERIFY_MSG` | Fatal checks where the expression must run in every build. | Same fatal path as `ASSERT` when enabled and false. |
| Interactive fatal assertions | `ASSERT_INTERACTIVE`, `ASSERT_INTERACTIVE_MSG`, `VERIFY_INTERACTIVE`, `VERIFY_INTERACTIVE_MSG` | Developer inspection paths where continuing may be useful. | Logger Fatal report, then Break, Abort, Ignore Once, or Always Ignore. |
| Recoverable checks | `CHECK`, `CHECK_MSG` | Diagnostics for failures that do not require stopping execution. | Logger Error report and continue. |
| Once-per-call-site checks | `CHECK_ONCE`, `CHECK_ONCE_MSG` | Noisy recoverable diagnostics. | First failed reporting attempt per macro expansion site, then continue. |
| Boolean checks | `ENSURE`, `ENSURE_MSG` | Recoverable validation when the caller needs the boolean result. | Evaluates once, reports false results when checks are enabled, and returns the result. |
| Impossible paths | `UNREACHABLE` | Control flow that should not execute. | Fatal unreachable path when assertions are enabled; trap or compiler unreachable hint when disabled. |
| Explicit break | `DEBUG_BREAK` | Intentional debugger breakpoints. | Forces the platform break/trap path independent of assert/check enablement. |

@ref assert_macros explains the macro families. @ref assert_macro_behavior owns the full expression-evaluation matrix.

## Runtime namespace

`GameWIP::Debug::Assert` contains the small typed runtime API used by interactive handling and explicit breakpoints:

| API | Purpose |
| --- | --- |
| `FailureAction` | Interactive action enum with `Break`, `Abort`, `IgnoreOnce`, and `AlwaysIgnore`. |
| `debugBreak() noexcept` | Calls the platform debugger break instruction when the runtime library is available. |

`debugBreak()` is the function used by `DEBUG_BREAK()` in runtime-enabled builds. Normal fatal assertion handling checks debugger state before
breaking; `DEBUG_BREAK()` intentionally force-breaks.

## Configuration API

The public macro behavior is selected by Assert CMake options and propagated compile definitions. Application code should configure the target instead
of redefining Assert macros locally.

See @ref assert_configuration for `ASSERT_ENABLED`, `ASSERT_CHECKS_ENABLED`, diagnostics, unreachable behavior, Windows manifest behavior, and
test-hook availability.

## Package and ABI surface

Installed consumers link `GameWIP::Assert`. Source-tree consumers may link `Assert`. Assert may be a shared runtime library or an interface-only
target depending on configuration.

The `Detail::handle*` exported symbols are ABI support for the public macros, not public C++ API. They are documented in @ref assert_abi because they
appear at the binary boundary.

## Related pages

- @ref assert_quick_start
- @ref assert_macros
- @ref assert_macro_behavior
- @ref assert_failure_actions
- @ref assert_abi
