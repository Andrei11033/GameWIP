@page test_support_child_processes Child processes

Child execution is configured through `Types::ChildProcessOptions`, returns `Types::ChildProcessResult`, and is implemented by the Win32 backend.

## Launch model

The executable is launched directly; no shell is involved. Shell expansion, pipelines, shell quoting, and redirection operators are not interpreted. TestSupport quotes the argument vector according to Windows command-line rules.

The child inherits the parent's working directory. `executablePath` uses native `std::filesystem::path` representation. Narrow arguments and environment names/values are UTF-8. Embedded nulls are rejected, and environment names must be non-empty and contain no `=`.

## Environment block

When `inheritParentEnvironment` is true, the parent block is copied before overrides. Otherwise the child starts from an otherwise empty block. Overrides are applied in vector order; later duplicates win. Name matching is case-insensitive on Windows. `std::nullopt` removes a key, while an engaged empty string creates an explicitly empty child value.

## Standard handles and capture

When capture is enabled, child stdout and stderr share one pipe. `output` stores retained raw bytes without UTF-8 validation or newline conversion. At most `maxCapturedOutputBytes` are retained; excess bytes continue to be drained. `outputTruncated` is true only when bytes were discarded because of this limit.

A capture failure is reported through `status` and may preserve partial output. `outputTruncated` does not mean capture failed.

When capture is disabled, the child receives inheritable duplicates of the selected parent standard handles. Other inheritable parent handles are excluded through an explicit allowlist.

## Timeout and process tree

A negative timeout waits indefinitely. Zero performs an immediate wait. Durations above the largest finite Win32 wait are clamped.

Timeout controls when termination begins; it is not a strict total-duration limit. TestSupport completes job termination and output-reader shutdown before returning. A successfully enforced timeout has successful infrastructure status and `ChildProcessOutcome::TimedOut`.

The process is assigned to a Win32 job before it is resumed. Failure recovery and timeouts terminate the job. Remaining descendants are also terminated after primary-process completion so they cannot retain capture handles indefinitely.

## Result interpretation

Interpret infrastructure status and child outcome independently:

| Situation | Status | Outcome | Exit code |
| --- | --- | --- | --- |
| Child exits zero or nonzero | Success | `Exited` | Exact native code |
| Timeout is enforced | Success | `TimedOut` | Not meaningful |
| Setup or launch fails before creation | Failed | `NotStarted` | Not meaningful |
| Failure recovery requests termination | Failed | `TerminatedDuringCleanup` | Not meaningful |
| Exit inspection fails after launch | `ProcessInspectionFailed` | `OutcomeUnavailable` | Not meaningful |
| Capture fails but exit inspection succeeds | `CaptureFailed` | `Exited` | Exact native code |

`nativeCode` retains the Win32 or standard-library diagnostic when available. Values from `0` through `0xffffffff` remain representable without colliding with child exit codes.

## Failure behavior

`runChildProcess()` is `noexcept`. Invalid input, unsupported platforms, allocation failure, setup, launch, pipe, capture, thread, wait, inspection, and cleanup failures are returned through `status`.

Caller-side construction of allocating option fields remains governed by their standard-library types. A failed result may retain useful child outcome, exit-code, output, and truncation evidence when those values were observed before the failure.

## Related pages

- @ref test_support_public_api
- @ref test_support_examples
- @ref test_support_troubleshooting
