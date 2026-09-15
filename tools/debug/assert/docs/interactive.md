@page assert_interactive Interactive asserts

Interactive assertions are developer failure paths for places where inspecting a failure and continuing may be useful. They are not a runtime recovery
mechanism.

## Public macros

| Macro | Condition evaluation | Interactive behavior |
| --- | --- | --- |
| `ASSERT_INTERACTIVE` | Skipped when assertions are disabled; may also be skipped after Always Ignore at the same call site. | Reports false results through the interactive action path. |
| `ASSERT_INTERACTIVE_MSG` | Same as `ASSERT_INTERACTIVE`; message is lazy diagnostic text. | Same as `ASSERT_INTERACTIVE`. |
| `VERIFY_INTERACTIVE` | Always evaluates once. | Reports false results through the interactive action path when assertions are enabled and the call site is not ignored. |
| `VERIFY_INTERACTIVE_MSG` | Same as `VERIFY_INTERACTIVE`; message is lazy diagnostic text. | Same as `VERIFY_INTERACTIVE`. |

## Action selection

Interactive failures report through Logger first. Assert then selects an action from the test-action environment, popup-suppression state, or the
platform action dialog. When a debugger is attached, the default action favors Break. Without a debugger, the default is Abort.

## Dialog behavior

On Windows, Assert prefers a Task Dialog with Break, Abort, Ignore Once, and Always Ignore. If that path is unavailable, Assert falls back to a
MessageBox-based dialog with a reduced action mapping.

The application-level Common Controls v6 requirement used by the preferred dialog is documented in @ref assert_configuration.

## Automation

Unattended tests must not depend on real dialogs. Maintainers should use the
environment-controlled behavior paths for interactive action selection; the
diagnostic-preparation emergency seam is documented in @ref assert_testing.

## Related pages

- @ref assert_failure_actions
- @ref assert_macro_behavior
- @ref assert_testing
