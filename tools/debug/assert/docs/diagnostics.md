@page assert_diagnostics Diagnostics

`ASSERT_DIAGNOSTICS` controls the diagnostic payload captured by Assert failure reports.

## Captured information

When diagnostics are enabled, failure reports can include condition text, custom message text, file, line, function, and failure category. When diagnostics are disabled, reports keep the failure category and intentionally omit caller-specific text.

Assert builds diagnostic messages with bounded storage. Long reports may be truncated with a visible suffix before they are sent to Logger or platform UI.

## Message evaluation

`_MSG` arguments are lazy diagnostic expressions. They are not evaluated when the macro is disabled, the condition passes, diagnostics are disabled, or reporting is suppressed before the message is needed.

```cpp
ASSERT_MSG(isValid(), buildExpensiveDiagnosticMessage());
```

`buildExpensiveDiagnosticMessage()` should not be required for program behavior.

## Logger reporting

Fatal assertion failures report at Logger Fatal severity through Logger's synchronous report path. Recoverable check failures report at Logger Error severity through the same synchronous path. This avoids leaving failure diagnostics queued while assertion handling proceeds to popup, break, abort, or continuation behavior.

## Popup policy

`ASSERT_POPUP_ON_ASSERT` controls whether fatal assertion reports may show Assert-owned UI. `ASSERT_POPUP_ON_CHECK` controls whether recoverable check failures may show UI. These are runtime compile definitions, not current cache options exposed by `tools/debug/assert/CMakeLists.txt`.

Automated tests should not depend on real UI. Use the validation hooks described in @ref assert_test_hooks when deterministic popup behavior is needed.

## Related pages

- @ref assert_configuration
- @ref assert_macro_behavior
- @ref assert_failure_actions
- @ref assert_interactive
