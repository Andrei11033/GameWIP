@page test_support_testing TestSupport testing

This page documents validation expectations for the TestSupport library itself.

## Normal tests

TestSupport tests should cover:

- runner and context result aggregation;
- expectation pass/fail behavior;
- report-file and console report options;
- file helper success and failure paths;
- scoped environment restore behavior;
- child-process exit, timeout, and output capture behavior;
- manual-check skipped behavior on non-interactive input;
- timer, section, start-gate, stop-flag, and worker helpers.

## CTest entry

TestSupport is validated through the project test executable:

```text
ctest --test-dir build-optimized-debuggable --output-on-failure
```

## Related pages

- @ref test_support_test_workflows
- @ref test_support_developer_validation
