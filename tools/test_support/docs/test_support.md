@page test_support TestSupport

TestSupport is the reusable validation-support library for GameWIP. It provides reporting and expectations, strict UTF-8 text fixtures, process/environment helpers, manual checks, timing, and small stress primitives.

The normal umbrella is `test_support/test_support.h`; focused public entry headers are `types.h`, `reporting.h`, `files.h`, `process.h`, and `stress.h` under the `test_support/` include directory.

TestSupport remains a static validation-oriented library and does not depend on Logger, Assert, IO, FileSystem, Window, Terminal, or engine systems. It may use foundational Unicode internally where actual UTF-8 text semantics require it.

## Consumer manual

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
- @subpage test_support_test_hooks
