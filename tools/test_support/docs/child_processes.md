@page test_support_child_processes Child processes

Child execution is configured through `Types::Process::Options`, returns `Types::Process::Result`, and is exposed by `runChildProcess()` from `test_support/process.h`.

## Launch model

The executable is launched directly; no shell is involved. Shell expansion, pipelines, shell quoting, and redirection operators are not interpreted. TestSupport builds the Win32 command line from the argument vector using Windows quoting rules. `executablePath` uses native `std::filesystem::path` representation. Narrow arguments and child environment names/values use UTF-8 at the Win32 conversion boundary. Embedded nulls and otherwise invalid process text are rejected by the process infrastructure contract.

When `inheritParentEnvironment` is true, the parent environment block is copied before overrides; otherwise the child begins with an otherwise empty block. `environmentOverrides` contains `Types::Process::EnvironmentOverride` values. An engaged value sets the child variable, including an explicitly empty value; `std::nullopt` removes it. Overrides are applied in vector order, later duplicate names win, and Win32 name matching is case-insensitive.

## Output capture is bytes

When capture is enabled, stdout and stderr share one capture pipe. `Types::Process::Result::outputBytes` contains retained raw bytes exactly as produced by the child; TestSupport does not validate, normalize, decode, or perform newline conversion on captured output.

At most `maxCapturedOutputBytes` bytes are retained. The pipe continues draining after that limit to prevent a verbose child from blocking. `outputTruncated` means bytes were discarded because of the retention limit, not that capture failed.

A capture infrastructure failure may still preserve useful partial `outputBytes` and a child outcome observed before the failure. When capture is disabled, the child receives inheritable duplicates of the selected parent standard handles; unrelated inheritable parent handles are excluded through the backend allowlist.

## Timeout and result interpretation

Interpret infrastructure status and process outcome independently:

| Situation | Status | `Types::Process::Outcome` | Exit code |
| --- | --- | --- | --- |
| Child exits zero or nonzero | Success | `Exited` | Exact native code |
| Timeout is enforced | Success | `TimedOut` | Not meaningful |
| Setup or launch fails before creation | Failed | `NotStarted` | Not meaningful |
| Failure recovery requests termination | Failed | `TerminatedDuringCleanup` | Not meaningful |
| Exit inspection fails after launch | `ProcessInspectionFailed` | `OutcomeUnavailable` | Not meaningful |
| Capture fails but exit inspection succeeds | `CaptureFailed` | `Exited` | Exact native code |

`runChildProcess()` remains `noexcept`; expected implementation allocation, setup, launch, capture, wait, inspection, and cleanup failures are returned through `status`. Timeout continues to be a successful domain outcome when enforcement succeeds. A negative timeout waits indefinitely, zero performs an immediate wait, and durations above the largest finite Win32 wait are clamped. Timeout determines when termination begins; job cleanup and capture-reader shutdown may extend total call duration.

The child is assigned to a Win32 job before it is resumed. Failure recovery and timeout enforcement terminate the job, and descendants are cleaned up so they cannot keep capture handles alive indefinitely. The Win32 backend owns the job, pipe, and handle implementation and preserves bounded capture and descendant cleanup. `nativeCode` retains the Win32 or standard-library diagnostic when available.

@ref test_support_public_api
@ref test_support_examples
