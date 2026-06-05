@page test_support_testing TestSupport testing

## Normal tests

TestSupport tests should cover:

- runner and context result aggregation;
- expectation pass/fail behavior;
- report buffering, suite-boundary flushing, and immediate per-line flushing;
- file helper success and failure paths;
- scoped environment restore behavior;
- child-process exit, timeout, bounded capture, continued draining, and descendant cleanup;
- manual-check skipped behavior on non-interactive input;
- timer, section, start-gate, stop-flag, and worker helpers.

## CTest entry

TestSupport is validated through the project test executable:

```text
ctest --test-dir build-optimized-debuggable --output-on-failure
```

TestSupport must remain independent of Logger, Assert, Terminal, and engine libraries. Child-process tests should assert portable observable outcomes: successful exit, nonzero exit, timeout, test-requested termination, captured output, and output truncation.

See @ref test_support_child_processes and @ref test_support_reports for the corresponding contracts.
