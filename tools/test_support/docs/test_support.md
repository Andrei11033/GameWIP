@page test_support TestSupport

`GameWIP::TestSupport` provides reusable infrastructure for executable test programs: reporting, expectations, isolated filesystem and environment state, child processes, manual checks, timing, and small concurrency helpers.

Include `test_support/test_support.h`. Passive configuration and result values live in `GameWIP::TestSupport::Types`; active runners, contexts, guards, process helpers, and stress helpers live directly in `GameWIP::TestSupport`.

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

## Generated API reference

Use @ref GameWIP::TestSupport for active helpers and @ref GameWIP::TestSupport::Types for report, suite, process, and manual-check values. The generated reference owns exact signatures and field declarations; the manual explains how the APIs compose and which observable behavior callers may rely on.

## Key behavior

- Expectations record one pass or failure and return a boolean; they do not abort the suite.
- `Runner` converts an uncaught suite exception into one failed check, records the completed suite, and permits later suites to run.
- Report-file failure disables only that file sink. It does not alter pass, failure, or skip counts.
- Filesystem, current-directory, and environment helpers are convenient test infrastructure. Process-global state still requires exclusive coordination by the test executable.
- Child-process and manual-input helpers can block. Child timeouts begin termination but are not strict upper bounds on total cleanup time.
- RAII cleanup and state restoration performed by destructors are best effort and cannot report failure.

## Dependency boundary

TestSupport is installed as the static target `GameWIP::TestSupport` and has no dependency on another GameWIP library. The installed package exports `test_support/test_support.h`; internal and platform headers are not consumer API.

The public interface exposes standard-library strings, paths, vectors, chrono values, source locations, synchronization-backed classes, and templates. Consumers therefore follow the compiler, standard-library, runtime, C++23, and exact-version policy described by @ref project_library_compatibility.

TestSupport is intended for test and validation executables. It does not replace production error handling, FileSystem's detailed status model, Logger, Assert, a benchmark framework, or a general process-management library. The current process/environment backend is Win32.
