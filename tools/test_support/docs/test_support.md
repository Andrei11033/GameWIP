@page test_support TestSupport

`GameWIP::TestSupport` provides reusable infrastructure for executable validation programs: reporting, expectations, strict UTF-8 text fixtures,
isolated filesystem and environment state, child processes, manual checks, timing, and small concurrency helpers.

The normal umbrella is `test_support/test_support.h`. Focused public entry headers are `types.h`, `reporting.h`, `files.h`, `process.h`, and
`stress.h` under the `test_support/` include directory. Passive values live in the single `GameWIP::TestSupport::Types` tree; active runners,
contexts, guards, process helpers, and stress helpers live directly in `GameWIP::TestSupport`.

## How the library is organized

A `Runner` owns suite registration, execution, aggregate results, and optional
report output. A suite receives a context and records expectations instead of
aborting at the first mismatch. Focused helpers build controlled test state:
temporary files, current-directory and environment guards, child processes,
manual observations, timing samples, and bounded stress runs. These helpers
make cleanup and diagnostics consistent, but they do not make process-global
state safe to mutate concurrently.

## Consumer manual

- @subpage test_support_quick_start — Include, link, register a suite, record an
  expectation, run it, and inspect the result.
- @subpage test_support_public_api — Find runners, contexts, reporting, files,
  process helpers, stress helpers, passive types, and status values.
- @subpage test_support_expectations — Understand pass/fail recording,
  comparisons, exceptions, diagnostics, and suite continuation.
- @subpage test_support_reports — Configure console/file reports and interpret
  counts, skips, failures, and report-sink errors.
- @subpage test_support_files_environment — Create isolated fixtures and restore
  files, directories, current directory, and environment state.
- @subpage test_support_child_processes — Launch, capture, time out, terminate,
  and clean up child-process trees.
- @subpage test_support_manual_tests — Record explicit human observations
  without mixing them into unattended validation.
- @subpage test_support_timing_stress — Measure work and run bounded repeated or
  concurrent scenarios.
- @subpage test_support_examples — See suites, expectations, fixtures,
  processes, reports, manual checks, and stress runs in context.
- @subpage test_support_troubleshooting — Diagnose state leakage, report
  failures, capture limits, timeouts, cleanup, and concurrency mistakes.

## Maintainer validation

- @subpage test_support_testing — See the library's own automated and package
  coverage.
- @subpage test_support_test_hooks — Understand source-tree-only injected
  failures and reset rules.

## Generated API reference

Use @ref GameWIP::TestSupport::Runner for the normal suite coordinator and @ref
GameWIP::TestSupport::Types::Reporting::SuiteResult for its passive result. The
class and namespace indexes contain every active helper and passive type.

## Key behavior

- Expectations record one pass or failure and return a boolean; they do not abort a suite.
- `Runner` converts an uncaught suite exception into one failed check, records the completed suite, and permits later suites to run.
- Report-file failure disables only that file sink and does not rewrite pass/fail/skip counts.
- Text-file helpers expose only valid UTF-8; child stdout/stderr capture remains arbitrary bytes in `Types::Process::Result::outputBytes`.
- Filesystem, current-directory, environment, and child-process helpers use TestSupport-owned infrastructure status. Process-global state still
  requires caller coordination.
- Child-process and manual-input helpers can block. Child timeouts begin termination but are not strict upper bounds on total cleanup time.
- RAII cleanup/restoration performed by destructors is best effort and non-throwing.

## Dependency boundary

TestSupport is installed as the static target `GameWIP::TestSupport`. It
privately depends on foundational Unicode to implement actual UTF-8 text
semantics, but does not depend on Logger, Assert, IO, FileSystem, Window,
Terminal, engine systems, or other higher-level GameWIP libraries. Its public
status/result model is locally owned, and its public headers do not expose
Unicode types.

The Win32 environment and child-process backends use GameWIP Unicode for strict UTF-8 to UTF-16 conversion. TestSupport continues to own argument
validation, process policy, infrastructure failures, and result construction; production code never depends on TestSupport.

TestSupport is intended for test and validation executables. It does not replace production error handling, FileSystem's richer policy/status surface,
Logger, Assert, a benchmark framework, or a general process-management library.

See @ref test_support_public_api and @ref test_support_quick_start.
