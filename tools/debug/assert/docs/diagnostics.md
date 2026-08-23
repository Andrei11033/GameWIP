@page assert_diagnostics Diagnostics

`ASSERT_DIAGNOSTICS` controls the diagnostic payload captured by Assert failure reports.

## Captured information

When diagnostics are enabled, failure reports can include condition text, custom message text, file, line, function, and failure category. Condition,
message, file, and function text follow the project UTF-8 text contract. When diagnostics are disabled, reports keep the failure category and
intentionally omit caller-specific text.

Assert builds diagnostic messages with bounded stack storage. Long reports are truncated only at complete UTF-8 scalar boundaries and receive a
visible ASCII suffix before they are sent to Logger or platform UI. Assert does not normalize text, rewrite BOMs, or add unconditional success-path
UTF-8 validation; callers and compiler-provided diagnostic text satisfy the UTF-8 precondition, and the bounded formatter only adjusts the cut point
when truncation is required.

On Win32, popup text is converted strictly from UTF-8 to UTF-16 at the native boundary. Popup length limits are then applied without separating a
UTF-16 surrogate pair, so native truncation cannot split a Unicode scalar.

## Message evaluation

`_MSG` arguments are lazy diagnostic expressions. They are not evaluated when the macro is disabled, the condition passes, diagnostics are disabled,
or reporting is suppressed before the message is needed.

```cpp
ASSERT_MSG(isValid(), buildExpensiveDiagnosticMessage());
```

`buildExpensiveDiagnosticMessage()` should not be required for program behavior.

## Logger reporting

Fatal assertion failures report at Logger Fatal severity through Logger's synchronous report path. Recoverable check failures report at Logger Error
severity through the same synchronous path. This avoids leaving failure diagnostics queued while assertion handling proceeds to popup, break, abort,
or continuation behavior.

## Popup policy

`ASSERT_POPUP_ON_ASSERT` controls whether fatal assertion reports may show
Assert-owned UI. `ASSERT_POPUP_ON_CHECK` controls whether recoverable check
failures may show UI. These are runtime compile definitions; the Assert CMake
target does not expose them as cache options.

Automated tests should not depend on real UI. Use the validation hooks described in @ref assert_test_hooks when deterministic popup behavior is
needed.

## Related pages

- @ref assert_configuration
- @ref assert_macro_behavior
- @ref assert_failure_actions
- @ref assert_interactive
