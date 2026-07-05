@page assert_macro_behavior Assert macro behavior

This is the most important Assert reference page. Use it when deciding whether an expression or message may have side effects.

## Evaluation table

| Macro | Enabled false behavior | Disabled behavior | Condition evaluated when disabled? | Message evaluated when disabled/pass? |
| --- | --- | --- | --- | --- |
| `ASSERT(expr)` | fatal report / popup policy / abort | skipped | no | n/a |
| `ASSERT_MSG(expr, msg)` | fatal report with message | skipped | no | no |
| `VERIFY(expr)` | fatal report / popup policy / abort | expression only | yes | n/a |
| `VERIFY_MSG(expr, msg)` | fatal report with message | expression only | yes | no |
| `CHECK(expr)` | recoverable report and continue | skipped | no | n/a |
| `CHECK_MSG(expr, msg)` | recoverable report with message | skipped | no | no |
| `CHECK_ONCE(expr)` | first report per call site and continue | skipped | no | n/a |
| `CHECK_ONCE_MSG(expr, msg)` | first report per call site with message | skipped | no | no |
| `ENSURE(expr)` | report false and return bool | returns bool | yes | n/a |
| `ENSURE_MSG(expr, msg)` | report false with message and return bool | returns bool | yes | no unless reporting |
| `ASSERT_INTERACTIVE(expr)` | interactive action choice | skipped | no | n/a |
| `ASSERT_INTERACTIVE_MSG(expr, msg)` | interactive action choice with message | skipped | no | no |
| `VERIFY_INTERACTIVE(expr)` | interactive action choice | expression only | yes | n/a |
| `VERIFY_INTERACTIVE_MSG(expr, msg)` | interactive action choice with message | expression only | yes | no unless reporting |
| `UNREACHABLE()` | fatal unreachable path | trap or compiler unreachable hint | n/a | n/a |
| `DEBUG_BREAK()` | break/trap path | break/trap path | n/a | n/a |

## Side-effect rules

Use this rule of thumb:

- If the expression must always run, use `VERIFY`, `VERIFY_INTERACTIVE`, or `ENSURE`.
- If the expression is only a debug contract, use `ASSERT`.
- If the failure is recoverable and no boolean result is needed, use `CHECK`.
- If the failure is recoverable and a boolean result is needed, use `ENSURE`.
- For one report per macro expansion, use `CHECK_ONCE`.

## Message rules

`_MSG` message expressions are intended for diagnostics. They should not be required for program behavior. Message expressions are evaluated only when the diagnostic path needs them and diagnostics are enabled.

This means message expressions are not evaluated for passing assertions/checks, disabled macros, or diagnostics-disabled builds. Keep required side effects in the condition expression or surrounding code, not in the message expression.

## Common mistakes

```cpp
// Bad if release builds still need pop() to run.
ASSERT(queue.pop());

// Better: pop() always runs; false still reports when assertions are enabled.
VERIFY(queue.pop());
```

```cpp
// Good: exactly one call and the result is useful to control flow.
if (!ENSURE(loadConfig()))
{
    return false;
}
```

## Related pages

- @ref assert_examples
- @ref assert_diagnostics
- @ref assert_failure_actions
