@page test_support TestSupport

`GameWIP::TestSupport` provides reusable infrastructure for executable validation programs: reporting, expectations, strict UTF-8 text fixtures, isolated filesystem and environment state, child processes, manual checks, timing, and small concurrency helpers.

The normal umbrella is `test_support/test_support.h`. Focused public entry headers are `types.h`, `reporting.h`, `files.h`, `process.h`, and `stress.h` under the `test_support/` include directory. Passive values live in the single `GameWIP::TestSupport::Types` tree; active runners, contexts, guards, process helpers, and stress helpers live directly in `GameWIP::TestSupport`.

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

## Key behavior

- Expectations record one pass or failure and return a boolean; they do not abort a suite.
- `Runner` converts an uncaught suite exception into one failed check, records the completed suite, and permits later suites to run.
- Report-file failure disables only that file sink and does not rewrite pass/fail/skip counts.
- Text-file helpers expose only valid UTF-8; child stdout/stderr capture remains arbitrary bytes in `Types::Process::Result::outputBytes`.
- Filesystem, current-directory, environment, and child-process helpers use TestSupport-owned infrastructure status. Process-global state still requires caller coordination.
- Child-process and manual-input helpers can block. Child timeouts begin termination but are not strict upper bounds on total cleanup time.
- RAII cleanup/restoration performed by destructors is best effort and non-throwing.

## Dependency boundary

TestSupport is installed as the static target `GameWIP::TestSupport`. It may depend on foundational Unicode to implement actual UTF-8 text semantics, but does not depend on Logger, Assert, IO, FileSystem, Window, Terminal, engine systems, or other higher-level GameWIP libraries. Its public status/result model remains locally owned, and its public headers do not expose Unicode types.

TestSupport is intended for test and validation executables. It does not replace production error handling, FileSystem's richer policy/status surface, Logger, Assert, a benchmark framework, or a general process-management library.

See @ref test_support_public_api and @ref test_support_quick_start.
