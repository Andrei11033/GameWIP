@page test_support_testing TestSupport maintainer validation

@note This page describes how maintainers validate TestSupport itself. The public TestSupport API remains documented in the user-manual pages.

The focused suite covers:

- runner/context result aggregation and section lifetime;
- expectation pass/fail, throw/no-throw, skip, manual, and continuation behavior;
- report buffering, suite-boundary flushing, immediate flushing, category filtering, and report-failure diagnostics;
- scoped temporary-directory creation, uniqueness, nested artifacts, and cleanup;
- scoped current-path and environment restoration;
- text-file helper success and failure;
- child exit, nonzero exit, timeout, bounded capture, continued draining, descendant cleanup, and requested termination;
- manual-check skip behavior for unattended input;
- timer, start gate, stop flag, and worker coordination.

TestSupport must remain independent of IO, FileSystem, Terminal, Logger, Assert, engine, and game code. Child-process tests assert portable observable results rather than private Win32 implementation details.

See @ref test_support_child_processes and @ref test_support_reports for public contracts. GameWIP's module routing and report policy are documented under @ref project_testing and @ref project_validation.
