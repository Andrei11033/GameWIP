@page test_support_manual_tests Manual checks

`promptManualCheck()` is declared in `test_support/reporting.h` and returns `Types::Reporting::ManualAnswer`:

- `Yes`
- `No`
- `Skipped`

The prompt retries unrecognized input, returns `Skipped` on end-of-input, does not trim whitespace, and may propagate standard-stream exceptions when
the caller configures those streams to throw.

The overload `promptManualCheck(question, skipReason)` clears and fills an owning reason string. It accepts `y`, `Y`, `yes`, or `Yes`; `n`, `N`,
`no`, or `No`; and `s`, `S`, `skip`, or `Skip`. Enter `s <reason>` or `skip <reason>` to retain a reason such as missing hardware or a tool
limitation. Bare skips report that no reason was supplied; EOF reports that input ended before an answer. Answers are not trimmed.

Manual tests remain opt-in validation behavior. Callers normally translate the answer into `Context::pass()`, `fail()`, or `skip()`.

@ref test_support_public_api
@ref test_support_testing
