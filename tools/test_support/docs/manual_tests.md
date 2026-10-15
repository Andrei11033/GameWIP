@page test_support_manual_tests Manual checks

`promptManualCheck()` is declared in `test_support/reporting.h` and returns `Types::Reporting::ManualAnswer`:

- `Yes`
- `No`
- `Skipped`

The prompt retries unrecognized input, returns `Skipped` on end-of-input, does not trim whitespace, and may propagate standard-stream exceptions when
the caller configures those streams to throw.

The overload `promptManualCheck(question, skipReason)` clears and fills an owning reason string.
Enter `s <reason>` or `skip <reason>` to retain a reason such as missing hardware, a tool limitation, or
an observation outside the selected scope. Bare skips report that no reason was supplied; EOF
reports that input ended before an answer. Existing yes/no/skip spellings remain accepted.

Manual tests remain opt-in validation behavior. Callers normally translate the answer into `Context::pass()`, `fail()`, or `skip()`.

@ref test_support_public_api
@ref test_support_testing
