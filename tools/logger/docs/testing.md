@page logger_testing Logger testing

Logger validation is split into correctness tests, stress tests, hook-forced tests, manual UI tests, and Google Benchmark scenarios.

## Normal tests

Normal tests cover public API behavior, configuration, formatting, filtering, reports, statistics, lifecycle, and file output.

Foundation integration tests use Terminal's shared test-hook state to verify severity styling, stdout/stderr routing, redirected plain-text fallback, UTF-8 text, native line endings, and one backend write per Logger record. File tests verify UTF-8 log-directory round trips and reader sharing while Logger retains its writer.

## Stress tests

Stress tests cover producer concurrency, flush while producers are active, shutdown while producers are active, queue pressure, and repeated init/shutdown.

## Test hooks

When `INTERNAL_LOGGER_TEST_HOOKS=1`, internal hooks can force rare failure paths such as file write failure, file flush failure, fatal popup failure, and timed flush timeout.

The `initDefault()` test changes into a scoped OS-temp working directory and verifies that the runtime default resolves to its `logs` child without modifying repository or build directories.

Test hooks are internal, compile-time gated, and not part of the production public API.

## Manual UI

The logger fatal popup is manually validated through a report path that requests `ReportPopup::Fatal`.

Manual UI tests must remain runtime opt-in. Automated tests should use hook-controlled failure paths and must not rely on real popup interaction.

## GameWIP integration

GameWIP owns the Logger test-module registration, runtime stress/UI selection, report location, benchmark executable, and coverage target. See @ref library_testing, @ref project_benchmarking, and @ref library_coverage.
