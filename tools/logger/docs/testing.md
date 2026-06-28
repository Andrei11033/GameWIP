@page logger_testing Logger testing

Logger validation is split into correctness tests, stress tests, hook-forced tests, manual UI tests, and Google Benchmark scenarios.

## Normal tests

Normal tests cover public API behavior, configuration, formatting, filtering, reports, statistics, lifecycle, and file output.

## Stress tests

Stress tests cover producer concurrency, flush while producers are active, shutdown while producers are active, queue pressure, and repeated init/shutdown.

## Test hooks

When `INTERNAL_LOGGER_TEST_HOOKS=1`, internal hooks can force rare failure paths such as file write failure, file flush failure, fatal popup failure, and timed flush timeout.

Test hooks are internal, compile-time gated, and not part of the production public API.

## Manual UI

The logger fatal popup is manually validated through a report path that requests `ReportPopup::Fatal`.

Manual UI tests must remain runtime opt-in. Automated tests should use hook-controlled failure paths and must not rely on real popup interaction.

## Coverage

Coverage is enabled at configure time with `GAMEWIP_ENABLE_COVERAGE=ON`; it is not a runtime option. The project-level coverage target writes:

```text
build-coverage/coverage/index.html
build-coverage/coverage/coverage.xml
```

See @ref library_coverage for the full command. gcov/gcovr may warn about ignored negative hits, but reports should still be generated when the target completes.
