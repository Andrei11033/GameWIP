@page assert_macro_behavior Macro behavior

Read this matrix whenever an Assert expression or message can change state. It
shows exactly what is evaluated, how often it is evaluated, and what remains in
a build where the corresponding family is disabled.

## Evaluation matrix

| Macro | Condition evaluated when enabled? | Condition evaluated when disabled? | Message evaluated on pass? | Message evaluated on reported failure? | Return value |
| --- | --- | --- | --- | --- | --- |
| `ASSERT(expr)` | yes | no | n/a | n/a | none |
| `ASSERT_MSG(expr, msg)` | yes | no | no | only when diagnostics are enabled | none |
| `VERIFY(expr)` | yes | yes | n/a | n/a | none |
| `VERIFY_MSG(expr, msg)` | yes | yes | no | only when diagnostics are enabled | none |
| `ASSERT_INTERACTIVE(expr)` | yes, unless Always Ignore suppresses the call site | no | n/a | n/a | none |
| `ASSERT_INTERACTIVE_MSG(expr, msg)` | yes, unless Always Ignore suppresses the call site | no | no | only when diagnostics are enabled and reporting is not suppressed | none |
| `VERIFY_INTERACTIVE(expr)` | yes | yes | n/a | n/a | none |
| `VERIFY_INTERACTIVE_MSG(expr, msg)` | yes | yes | no | only when diagnostics are enabled and reporting is not suppressed | none |
| `CHECK(expr)` | yes | no | n/a | n/a | none |
| `CHECK_MSG(expr, msg)` | yes | no | no | only when diagnostics are enabled | none |
| `CHECK_ONCE(expr)` | yes | no | n/a | n/a | none |
| `CHECK_ONCE_MSG(expr, msg)` | yes | no | no | only for the first reported failure when diagnostics are enabled | none |
| `ENSURE(expr)` | yes, exactly once | yes, exactly once | n/a | n/a | `bool` |
| `ENSURE_MSG(expr, msg)` | yes, exactly once | yes, exactly once | no | only when diagnostics are enabled | `bool` |
| `UNREACHABLE()` | n/a | n/a | n/a | n/a | does not return by contract |
| `DEBUG_BREAK()` | n/a | n/a | n/a | n/a | none |

For this table, disabled means the relevant family is disabled: `ASSERT_ENABLED=0` for fatal assertion families and `ASSERT_CHECKS_ENABLED=0` for recoverable check families.

## Failure behavior matrix

| Macro family | Enabled false behavior | Disabled behavior |
| --- | --- | --- |
| `ASSERT`, `VERIFY` | Logger Fatal report, optional popup, debugger break when attached, then abort. | `ASSERT` skipped; `VERIFY` evaluates only. |
| Interactive fatal macros | Logger Fatal report, action selection, then Break, Abort, Ignore Once, or Always Ignore. | `ASSERT_INTERACTIVE` skipped; `VERIFY_INTERACTIVE` evaluates only. |
| `CHECK` | Logger Error report and continue. | skipped. |
| `CHECK_ONCE` | First failed reporting attempt per macro expansion site reports and continues. | skipped. |
| `ENSURE` | Reports false results and returns the evaluated boolean. | returns the evaluated boolean without reporting. |
| `UNREACHABLE` | Fatal unreachable report. | trap or compiler unreachable hint. |
| `DEBUG_BREAK` | Force break/trap path. | same. |

## Side-effect rules

- Use `VERIFY`, `VERIFY_INTERACTIVE`, or `ENSURE` when the expression must always run.
- Use `ASSERT` when the expression is only a development contract.
- Use `CHECK` when the failure is recoverable and no boolean result is needed.
- Use `ENSURE` when recoverable control flow needs the boolean result.
- Use `CHECK_ONCE` only when repeated reports from the same macro expansion site would hide more useful diagnostics.

Message expressions are diagnostic-only. Keep required side effects in the condition expression or surrounding code.

## Examples

```cpp
// Bad if release-style builds still need pop() to run.
ASSERT(queue.pop());

// Better: pop() always runs; false still reports when assertions are enabled.
VERIFY(queue.pop());
```

```cpp
if (!ENSURE(loadConfig()))
{
    return false;
}
```

## Related pages

- @ref assert_macros
- @ref assert_diagnostics
- @ref assert_failure_actions
