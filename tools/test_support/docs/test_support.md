@page test_support TestSupport

The GameWIP TestSupport library is the shared support layer for GameWIP test executables.

It provides generic test reporting, expectations, suite running, file helpers, scoped environment changes, child process execution, manual checks, timing metrics, and small stress-test helpers. It does not depend on Logger or Assert, and it does not contain Logger-specific, Assert-specific, or engine-simulation test logic.

## User manual

- @subpage test_support_quick_start
- @subpage test_support_public_api
- @subpage test_support_expectations
- @subpage test_support_reports
- @subpage test_support_files_environment
- @subpage test_support_child_processes
- @subpage test_support_manual_tests
- @subpage test_support_timing_stress
- @subpage test_support_examples

## Key behavior

Expectations record failures and let the suite continue. Runtime options control what a test run does. CMake controls only compile-time and build features.

Main public entry points:

- `GameWIP::TestSupport::Runner` owns a shared report and aggregates suites.
- `GameWIP::TestSupport::Context` records suite lines, expectations, failures, and skips.
- `GameWIP::TestSupport::Section` groups large scenarios and reports section timing.
- `GameWIP::TestSupport::Timer` and `Types::IterationMetric` support metrics without hard thresholds.
- `GameWIP::TestSupport::Types::ReportOptions` controls console output, report-file output, append mode, and report path.
- `GameWIP::TestSupport::Types::ChildProcessOptions` and `runChildProcess()` support isolated process tests.
- `GameWIP::TestSupport::ScopedEnvironmentVariable` and `ScopedUnsetEnvironmentVariable` make temporary environment changes exception-safe.
- `GameWIP::TestSupport::StartGate`, `StopFlag`, and `runWorkers()` support small stress-test patterns.
