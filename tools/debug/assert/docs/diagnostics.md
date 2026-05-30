@page assert_diagnostics Assert diagnostics

Diagnostics are controlled by `GAMEWIP_ASSERT_DIAGNOSTICS`, normally through the Assert CMake target.

## Captured information

When diagnostics are enabled, failure reports can include:

- condition text,
- custom message text,
- file,
- line,
- function,
- failure category.

## Message evaluation

Message expressions are intentionally lazy. They are not evaluated when:

- the macro is disabled,
- the condition passes,
- diagnostics are disabled,
- the macro path does not need the message.

Example:

```cpp
ASSERT_MSG(isValid(), buildExpensiveDiagnosticMessage());
```

`buildExpensiveDiagnosticMessage()` should run only when the assertion is enabled, fails, and diagnostics are enabled.

## Popup policy

`GAMEWIP_ASSERT_POPUP_ON_ASSERT` controls whether fatal assertion reports may show UI. `GAMEWIP_ASSERT_POPUP_ON_CHECK` controls whether recoverable check failures may show UI. Automated tests should not rely on real UI.

Fatal assertion and check failures report through Logger's synchronous report path. This keeps failure diagnostics out of the async queue and writes them immediately before Abort, Break, Ignore, or continuation behavior is applied.

## Disabled diagnostics

When `GAMEWIP_ASSERT_DIAGNOSTICS=0`, the runtime still reports failure categories, but condition text, custom message text, file, line, and function details are intentionally omitted. Message expressions should not run in this mode.

## Related pages

- @ref assert_macro_behavior
- @ref assert_interactive
