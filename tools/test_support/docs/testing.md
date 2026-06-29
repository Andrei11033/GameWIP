@page test_support_testing TestSupport testing

## Normal tests

TestSupport tests should cover:

- runner and context result aggregation;
- expectation pass/fail behavior;
- report buffering, suite-boundary flushing, and immediate per-line flushing;
- minimal/concise/full console category filtering and report-failure diagnostics;
- file helper success and failure paths;
- scoped temporary-directory creation, uniqueness, nested artifacts, and cleanup;
- scoped current-path change, previous-path query, and restoration;
- scoped environment restore behavior;
- child-process exit, timeout, bounded capture, continued draining, and descendant cleanup;
- manual-check skipped behavior on non-interactive input;
- timer, section, start-gate, stop-flag, and worker helpers.

TestSupport must remain independent of every project library, including IO, FileSystem, Terminal, Logger, Assert, engine, and game code. Its standard-library and private platform-backend implementation keeps it usable as the standalone support layer for tests of those libraries. Child-process tests should assert portable observable outcomes: successful exit, nonzero exit, timeout, test-requested termination, captured output, and output truncation.

See @ref test_support_child_processes and @ref test_support_reports for the corresponding contracts.

## GameWIP integration

GameWIP owns the TestSupport test-module registration, child-protocol routing, CTest entry, report relocation into the OS-temp project root, and startup-validation policy. See @ref library_testing and @ref project_validation.
