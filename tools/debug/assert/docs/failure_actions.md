@page assert_failure_actions Assert failure actions

Interactive failures use `GameWIP::Debug::Assert::FailureAction`. Non-interactive fatal failures do not ask for an action.

## Non-interactive fatal path

A failed `ASSERT`, `VERIFY`, or enabled `UNREACHABLE()`:

1. Builds the failure diagnostic.
2. Reports synchronously through Logger at Fatal severity.
3. Shows Assert-owned UI when fatal popups are compiled on and not suppressed.
4. Breaks only when a debugger is attached.
5. Terminates with `std::abort()`.

## Recoverable path

A failed `CHECK`, reported `CHECK_ONCE`, or failed enabled `ENSURE`:

1. Builds the failure diagnostic.
2. Reports synchronously through Logger at Error severity.
3. Shows Assert-owned UI only when recoverable-check popups are compiled on and not suppressed.
4. Continues execution.

## Interactive actions

| Action | Behavior | Typical use |
| --- | --- | --- |
| `Break` | Enters the platform debugger break path and continues if execution resumes. | Inspect the failure immediately. |
| `Abort` | Terminates the process with `std::abort()`. | Stop when continuing is unsafe or unattended. |
| `IgnoreOnce` | Continues this failure only. | Step past a known transient failure. |
| `AlwaysIgnore` | Suppresses future interactive reports from the same macro expansion site. | Continue debugging without repeated prompts from one site. |

`AlwaysIgnore` is per call site. It does not affect normal `ASSERT`, `VERIFY`, `CHECK`, or another interactive macro expansion.

## Related pages

- @ref assert_interactive
- @ref assert_macro_behavior
- @ref assert_diagnostics
