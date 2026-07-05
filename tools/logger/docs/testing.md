@page logger_testing Logger maintainer validation

@note This page is for maintainers. Internal hooks are source-tree validation interfaces, not consumer API.

## Correctness coverage

The Logger suite covers:

- configuration presets, initialization, repeated lifecycle calls, and output modes;
- source registration, severity/source/level filters, compile-time formats, and runtime formats;
- asynchronous queue acceptance, filtering, pressure, soft/hard drops, and peak depth;
- console/file output, UTF-8 paths, reader sharing, redirection, styles, and line endings;
- synchronous reports, filter bypass, queue bypass, flush results, fatal reporting, and termination paths;
- allocation, format, file, popup, unknown-source, and truncation counters;
- counter reset rules and lifetime drop preservation.

## Concurrency and stress

Stress scenarios cover concurrent producers, reporting during production, timed flush while producers remain active, final drain, shutdown during activity, queue pressure, and repeated initialization/shutdown. They prove safety and progress; machine-dependent timing is not a correctness threshold.

## Performance review checklist

- Filtered macro calls avoid message and argument evaluation.
- Accepted producer calls avoid unnecessary allocation.
- Direct formatted calls account for argument evaluation before Logger checks filters.
- Queue-drop statistics exclude filtered messages.
- Instrumented validation builds are not used as final performance baselines.
- Long-message tests cover the active `FormatPolicy`; the bounded policy reduces peak memory, while the fast-normal policy favors common-case formatting speed.
- FileSystem and Terminal work remains on the worker or synchronous report path so normal producers do not gain I/O overhead.

## Hook-forced and manual paths

When `INTERNAL_LOGGER_TEST_HOOKS=1`, source-tree tests force rare allocation, file open/write/flush, popup, and timed-flush paths. Every scenario resets forced state.

The fatal popup is a runtime opt-in manual test. Automated jobs never rely on a real popup.

## Project integration

GameWIP owns module registration, runtime selection, reports, benchmarks, and coverage. See @ref project_testing, @ref project_benchmarking, and @ref project_coverage.
