@page logger_testing Maintainer validation

@note Logger validation uses source-tree hooks and is not installed consumer API.

## Correctness coverage

Validation covers normal/disabled initialization, checked invalid configuration, adjustment flags and effective limits, file setup fallback with
directly associated failure status, AlreadyOpen lifecycle rejection, shutdown/reinitialize, and health epoch reset.

It covers direct IO statuses for runtime filters, concurrent filter mutation with producers, UTF-8 configuration/source validation, scalar-safe
message truncation, normal file output, and queue-backed producer behavior.

Flush validation distinguishes completed, timed-out, invalid-timeout, and sink-failure cases. Report validation checks direct delivery status,
sink/popup failure, health degradation, formatting, and the emergency-path invariant that a timed report does not drain an older deliberately blocked
async record.

Fatal termination remains isolated in a child process; the real popup path remains manual/opt-in. Failure hooks remain one-shot and resettable.

The Logger correctness module stays one logical module while behavior-focused private `.inl` fragments keep configuration,
output, filters, reports, health, and process/manual cases easier to scan without promoting Logger-specific fixtures to TestSupport.

## Package and header validation

Project validation must check self-containment of `logger/types.h`, `logger/config.h`,
`logger/logger.h`, and `logger/logger_macros.h`, plus installed `GameWIP::Logger` consumption, exact IO/Terminal dependency resolution,
exported-symbol allowlists, sanitizer builds, and Logger benchmarks.

## Performance review

The hot normal logging/filter path must not gain structured results, UTF-8 whole-message scans, or success-path diagnostic allocation. Benchmarks
retain output-disabled, filtered formatted, accepted file, registered-source, and multi-producer scenarios.

## Related pages

- @ref logger_test_hooks
- @ref logger_threading_performance
- @ref logger_abi
