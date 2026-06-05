@page test_support_developer_validation TestSupport developer validation

Use this page for maintainer-focused TestSupport validation.

## Focused run

Run the project test executable through CTest:

```text
ctest --test-dir build-optimized-debuggable --output-on-failure
```

## Validation scope

TestSupport should remain independent of Logger, Assert, and engine systems. Tests may use TestSupport to validate other libraries, but TestSupport docs and examples should stay generic.

Child-process tests should validate observable portable outcomes: successful exit, nonzero exit, timeout, test-requested termination, and captured output.

## Related pages

- @ref test_support_testing
- @ref test_support_child_processes
- @ref test_support_reports
