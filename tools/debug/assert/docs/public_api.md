@page assert_public_api Assert public API

Include `debug/assert/assert.h` and link `GameWIP::Assert` from an installed package. The assertion macros are global so call sites remain concise; runtime support lives in `GameWIP::Debug::Assert`. The source tree also provides the short `Assert` target.

## Choosing a failure path

Use `ASSERT` for development invariants whose expression may disappear when assertions are disabled. Use `VERIFY` when the expression must execute in every build.

Use `CHECK` for recoverable diagnostics and `CHECK_ONCE` when repeated failures from one call site would obscure later output. Use `ENSURE` when the caller needs the evaluated boolean result in every build.

Interactive variants are developer workflows that may continue after Break, Ignore Once, or Always Ignore. They are not a substitute for recoverable runtime error handling.

`UNREACHABLE()` marks impossible control flow. `DEBUG_BREAK()` is an unconditional developer breakpoint and is independent of assertion enablement.

## Configuration boundary

Configure `ASSERT_*` behavior through the project CMake options. Application code should not redefine those macros after including the header.

Fatal failures report synchronously through Logger before debugger, popup, or termination handling. Recoverable checks report and continue. Message expressions are evaluated only on the enabled failure paths that need them; the exact expression-evaluation matrix is in @ref assert_macro_behavior.

`FailureAction` and `debugBreak()` are the small runtime API used by interactive handling and explicit breakpoints.

See @ref assert_macros, @ref assert_failure_actions, and @ref assert_interactive for the remaining contracts.
