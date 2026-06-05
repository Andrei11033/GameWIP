@page assert_public_api Assert public API guide

This page explains the Assert macro families, runtime types, and configuration macros in one place.

## Include file

```cpp
#include "debug/assert/assert.h"
```

The primary API is macro-based. The macros are intentionally global because assertion syntax should stay short at call sites.

## API family map

| Family | Public API | Primary behavior |
| --- | --- | --- |
| Runtime support | `GameWIP::Debug::Assert::FailureAction`, `debugBreak()` | Interactive action values and the runtime debug-break function used by `DEBUG_BREAK()`. |
| Fatal assertions | `ASSERT`, `ASSERT_MSG`, `VERIFY`, `VERIFY_MSG`, `UNREACHABLE` | Fatal diagnostics for invariants and impossible control flow. |
| Interactive assertions | `ASSERT_INTERACTIVE`, `ASSERT_INTERACTIVE_MSG`, `VERIFY_INTERACTIVE`, `VERIFY_INTERACTIVE_MSG` | Fatal diagnostics with developer-selected Break, Abort, Ignore Once, or Always Ignore actions. |
| Recoverable checks | `CHECK`, `CHECK_MSG`, `CHECK_ONCE`, `CHECK_ONCE_MSG`, `ENSURE`, `ENSURE_MSG` | Error diagnostics that continue execution; `ENSURE` returns the checked boolean. |
| Debug break | `DEBUG_BREAK` | Explicit debugger/trap breakpoint independent of assertion settings. |
| Configuration | `GAMEWIP_ASSERT_*` macros | Compile-time behavior switches normally set by CMake. |

## Configuration macros

| Macro | Purpose |
| --- | --- |
| `GAMEWIP_ASSERT_RUNTIME` | Indicates whether the assert runtime library is available. Normally set by CMake. |
| `GAMEWIP_ASSERT_ENABLED` | Enables fatal assertion failure handling. Defaults to on in non-`NDEBUG` builds. |
| `GAMEWIP_ASSERT_CHECKS_ENABLED` | Enables recoverable check reporting. Defaults to on in non-`NDEBUG` builds. |
| `GAMEWIP_ASSERT_DIAGNOSTICS` | Controls condition/file/line/function/message diagnostic text. |
| `GAMEWIP_ASSERT_POPUP_ON_ASSERT` | Allows fatal assert paths to show platform UI. |
| `GAMEWIP_ASSERT_POPUP_ON_CHECK` | Allows recoverable check paths to show platform UI. Disabled by default. |
| `GAMEWIP_ASSERT_UNREACHABLE_ASSUME` | Lets disabled `UNREACHABLE()` use compiler unreachable assumptions instead of a trap. |
| `GAMEWIP_ASSERT_TEST_HOOKS` | Enables internal test-hook declarations for dedicated test builds only. |

Normal users should configure these through CMake options instead of editing headers directly.

## Runtime public types and functions

### `GameWIP::Debug::Assert::FailureAction`

Interactive fatal assertions can return one of four actions:

| Action | Behavior |
| --- | --- |
| `Break` | Enters the debugger break path and continues if execution resumes. |
| `Abort` | Terminates with `std::abort()`. |
| `IgnoreOnce` | Continues this failure only. The same call site can report again later. |
| `AlwaysIgnore` | Continues and suppresses future interactive failures from the same macro expansion site. |

### `GameWIP::Debug::Assert::debugBreak()`

Triggers the platform debugger break instruction. Most code should use `DEBUG_BREAK()` instead because the macro documents intent at the call site and remains consistent with the rest of the assert API.

## Fatal assertion macros

### Fatal macro overload matrix

| Macro form | Enabled expression evaluation | Disabled expression evaluation | Message evaluation | Failure behavior |
| --- | --- | --- | --- | --- |
| `ASSERT(condition)` | Evaluates condition. | Does not evaluate condition. | No message expression. | Fatal Logger report, optional popup, debugger break only when attached, then abort. |
| `ASSERT_MSG(condition, message)` | Evaluates condition. | Does not evaluate condition or message. | Message is evaluated only on failure when diagnostics need it. | Same as `ASSERT`. |
| `VERIFY(condition)` | Evaluates condition. | Evaluates condition. | No message expression. | Same fatal path as `ASSERT` when enabled and false. |
| `VERIFY_MSG(condition, message)` | Evaluates condition. | Evaluates condition, not message. | Message is evaluated only on failure when diagnostics need it. | Same fatal path as `VERIFY`. |
| `UNREACHABLE()` | No condition. | Uses configured trap/unreachable hint. | No message expression. | Fatal assertion path when enabled. |

### `ASSERT(condition)`

Use `ASSERT` for invariants that should never be false during development. When assertions are enabled, the condition is evaluated. A false result synchronously reports through Logger at Fatal severity, may show the assert popup, breaks only when a debugger is attached, and then aborts.

When assertions are disabled, the condition is not evaluated.

### `ASSERT_MSG(condition, message)`

Same as `ASSERT`, but includes a custom message when diagnostics are enabled. The message expression is evaluated only on failure and only when the macro path needs it.

### `VERIFY(condition)`

Use `VERIFY` when the expression must always execute because it has side effects or performs required work. The expression is evaluated in all builds. When assertions are enabled and the result is false, it reports through the fatal assertion path.

### `VERIFY_MSG(condition, message)`

Same as `VERIFY`, but includes a custom diagnostic message on failure.

## Recoverable check macros

### Recoverable macro overload matrix

| Macro form | Enabled expression evaluation | Disabled expression evaluation | Message evaluation | Failure behavior |
| --- | --- | --- | --- | --- |
| `CHECK(condition)` | Evaluates condition. | Does not evaluate condition. | No message expression. | Error Logger report, then continue. |
| `CHECK_MSG(condition, message)` | Evaluates condition. | Does not evaluate condition or message. | Message is evaluated only on failure when diagnostics need it. | Same as `CHECK`. |
| `CHECK_ONCE(condition)` | Evaluates condition until first reported failure, then suppresses later reports from that call site. | Does not evaluate condition. | No message expression. | First failure reports Error, then all calls continue. |
| `CHECK_ONCE_MSG(condition, message)` | Same as `CHECK_ONCE`. | Does not evaluate condition or message. | Message is evaluated only for the first reported failure. | Same as `CHECK_ONCE`. |
| `ENSURE(condition)` | Evaluates condition exactly once. | Evaluates condition exactly once. | No message expression. | Reports Error when enabled and false, then returns bool. |
| `ENSURE_MSG(condition, message)` | Evaluates condition exactly once. | Evaluates condition exactly once, not message. | Message is evaluated only on enabled false results when diagnostics need it. | Same as `ENSURE`. |

### `CHECK(condition)`

Use `CHECK` for recoverable diagnostics. When checks are enabled and the condition is false, it synchronously reports through Logger at Error severity and continues execution. When checks are disabled, the condition is not evaluated.

### `CHECK_MSG(condition, message)`

Same as `CHECK`, but includes a custom message on failure.

### `CHECK_ONCE(condition)`

Use `CHECK_ONCE` for warnings that would otherwise spam logs. It reports only the first failure attempt from that macro expansion site.

The suppression flag is per call site and thread-safe. Under concurrency, one thread wins the first report attempt; later attempts are suppressed.

### `CHECK_ONCE_MSG(condition, message)`

Same as `CHECK_ONCE`, but includes a custom message on the first failure.

### `ENSURE(condition)`

Use `ENSURE` when you want a recoverable boolean result and optional reporting.

`ENSURE` always evaluates the condition exactly once and returns the boolean result. When checks are enabled and the result is false, it reports through the recoverable check path.

```cpp
if (!ENSURE(loadConfig()))
{
    return false;
}
```

### `ENSURE_MSG(condition, message)`

Same as `ENSURE`, but includes a custom message on false results.

## Interactive assertion macros

### Interactive overload matrix

| Macro form | Enabled expression evaluation | Disabled expression evaluation | Message evaluation | Failure choices |
| --- | --- | --- | --- | --- |
| `ASSERT_INTERACTIVE(condition)` | Evaluates condition unless the call site is Always Ignored. | Does not evaluate condition. | No message expression. | Break, Abort, Ignore Once, Always Ignore. |
| `ASSERT_INTERACTIVE_MSG(condition, message)` | Same as `ASSERT_INTERACTIVE`. | Does not evaluate condition or message. | Message is evaluated only on failure when diagnostics need it. | Same choices. |
| `VERIFY_INTERACTIVE(condition)` | Evaluates condition. | Evaluates condition. | No message expression. | Same choices when enabled and false. |
| `VERIFY_INTERACTIVE_MSG(condition, message)` | Evaluates condition. | Evaluates condition, not message. | Message is evaluated only on failure when diagnostics need it. | Same choices. |

Interactive assertions are developer tools for failures where continuing may be useful while debugging.

### `ASSERT_INTERACTIVE(condition)`

When assertions are enabled, evaluates the condition. A false result synchronously reports at Fatal severity and asks for a `FailureAction`: Break, Abort, Ignore Once, or Always Ignore.

When assertions are disabled, the condition is not evaluated.

### `ASSERT_INTERACTIVE_MSG(condition, message)`

Same as `ASSERT_INTERACTIVE`, but includes a custom message on failure.

### `VERIFY_INTERACTIVE(condition)`

Always evaluates the condition. When assertions are enabled and the result is false, it enters the same interactive fatal path.

### `VERIFY_INTERACTIVE_MSG(condition, message)`

Same as `VERIFY_INTERACTIVE`, but includes a custom message on failure.

## Control-flow and debugger macros

### `UNREACHABLE()`

Use `UNREACHABLE()` for control flow that should be impossible. Enabled builds report through the fatal assertion path. Disabled builds use the configured trap/unreachable hint behavior.

### `DEBUG_BREAK()`

Forces the debug break path regardless of assert/check settings. Use it for explicit developer breakpoints in code where a normal assertion is not the right expression.

## Evaluation summary

| Macro | Disabled expression evaluation | Enabled false behavior |
| --- | --- | --- |
| `ASSERT` / `ASSERT_MSG` | Not evaluated | Fatal report, optional popup, debugger break when attached, abort |
| `VERIFY` / `VERIFY_MSG` | Evaluated | Fatal report path |
| `CHECK` / `CHECK_MSG` | Not evaluated | Error report, continue |
| `CHECK_ONCE` / `CHECK_ONCE_MSG` | Not evaluated | First failure reports, continue |
| `ENSURE` / `ENSURE_MSG` | Evaluated once | Error report when checks enabled, returns bool |
| `ASSERT_INTERACTIVE` | Not evaluated | Fatal report, then selected action |
| `VERIFY_INTERACTIVE` | Evaluated | Fatal report and selected action when enabled |
| `UNREACHABLE` | Trap or compiler hint depending on config | Fatal report path |
| `DEBUG_BREAK` | Always executes break path | Break path |

## Automated and manual interactive behavior

Automated tests should use `GAMEWIP_ASSERT_TEST_ACTION` to force `break`, `abort`, `ignore_once`, or `always_ignore` without clicking real UI.

Manual UI validation is separate. It shows real platform dialogs and must be enabled by runtime test options.

## Related pages

- @ref assert_quick_start
- @ref assert_macro_behavior
- @ref assert_interactive
- @ref assert_failure_actions
- @ref assert_examples
