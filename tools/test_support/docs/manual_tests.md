@page test_support_manual_tests Manual checks

`promptManualCheck()` is declared in `test_support/reporting.h` and returns `Types::Reporting::ManualAnswer`:

- `Yes`
- `No`
- `Skipped`

The prompt retries unrecognized input, returns `Skipped` on end-of-input, does not trim whitespace, and may propagate standard-stream exceptions when
the caller configures those streams to throw.

Manual tests remain opt-in validation behavior. Callers normally translate the answer into `Context::pass()`, `fail()`, or `skip()`.

@ref test_support_public_api
@ref test_support_testing
