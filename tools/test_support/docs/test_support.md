@page test_support TestSupport

TestSupport is the shared support layer for test executables.

It provides generic test reporting, expectations, suite running, file helpers, scoped environment changes, child process execution, manual checks, timing metrics, and small stress-test helpers. It does not depend on Logger or Assert, and it does not contain Logger-specific, Assert-specific, or engine-simulation test logic.

## Documentation

- @subpage test_support_quick_start
- @subpage test_support_public_api
- @subpage test_support_expectations
- @subpage test_support_reports
- @subpage test_support_files_environment
- @subpage test_support_child_processes
- @subpage test_support_manual_tests
- @subpage test_support_timing_stress
- @subpage test_support_examples
- @subpage test_support_troubleshooting
- @subpage test_support_testing

## Key behavior

Expectations record failures and let the suite continue. Runtime options control what a test run does. CMake controls only compile-time and build features.
