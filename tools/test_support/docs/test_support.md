@page test_support TestSupport

TestSupport is the shared support layer for test executables.

It provides generic test reporting, expectations, suite running, file helpers, scoped environment changes, child process execution, manual checks, timing metrics, and small stress-test helpers. It depends on no other project library and contains no foundation-, tool-, engine-, or game-specific test logic.

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
- @subpage test_support_troubleshooting

## Maintainer validation

- @subpage test_support_testing

## Generated API reference

Use @ref GameWIP::TestSupport for active contexts, runners, guards, process helpers, timing, and stress helpers, and @ref GameWIP::TestSupport::Types for report options and result shapes. These generated pages document every public type, enum value, field, function, overload, and constant from `test_support/test_support.h`; the manual pages above explain complete workflows and edge cases.

## Key behavior

Expectations record failures and let the suite continue. Runtime options control what a test run does. CMake controls only compile-time and build features.
