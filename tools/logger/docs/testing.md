@page logger_testing Maintainer validation

@note Logger validation uses source-tree interfaces. Internal hooks and implementation headers are not installed consumer API.

## Correctness coverage

The Logger validation module covers:

- configuration presets, sanitization, effective limits, output modes, file fallback, and repeated lifecycle calls;
- source registration, string/ID/enum sources, initial and runtime filters, and unknown IDs;
- preformatted, compile-time, runtime, nested, bounded, and truncating formatting paths;
- asynchronous acceptance, ordered publication, worker filtering, soft/hard pressure, allocation skip markers, and final drain;
- console/file routing, UTF-8 paths, collision-safe files, read sharing, redirection, styles, line endings, write and flush failure;
- synchronous reports, debugger mirroring, bounded drains, popup handling, and termination child paths;
- every public statistics and memory-diagnostic field;
- macro compile-out and lazy-evaluation rules.

## Concurrency and stress

Stress scenarios cover concurrent producers, reports during production, timed flush with active producers, worker wait transitions, shutdown during a final producer departure, queue pressure, and repeated initialization/shutdown.

These scenarios validate safety and progress. Machine-dependent timing and throughput are not correctness thresholds.

Coverage includes concurrent source/level filter togglers with producers, a deterministic filter change after queueing but before worker delivery, and registered-`SourceId` multi-producer contention benchmarks.

## Package and header validation

Project validation also checks:

- public-header self-containment;
- installed-package consumption through `GameWIP::Logger`;
- exact dependency/package behavior;
- exported-symbol allowlists where configured.

See @ref project_testing for module selection and child-process protocol, @ref project_coverage for coverage workflow, and @ref project_benchmarking for performance runs.

## Manual paths

Real fatal popup behavior is a runtime opt-in manual test. Automated validation uses hooks and isolated child processes instead of depending on interactive UI or terminating the parent test process.

## Performance review

Instrumented validation builds are not final performance baselines. Benchmark/review should separately consider:

- filtered macro cost;
- producer formatting cost;
- source representation;
- queue pressure and batching;
- file flush policy;
- `FormatPolicy` peak memory;
- retained storage choices.

## Related pages

- @ref logger_test_hooks
- @ref logger_threading_performance
- @ref logger_abi
