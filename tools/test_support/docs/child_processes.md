@page test_support_child_processes Child processes

Child execution is configured through `Types::Process::Options`, returns `Types::Process::Result`, and is exposed by `runChildProcess()` from `test_support/process.h`.

## Launch model

The executable is launched directly; no shell is involved. `executablePath` uses native `std::filesystem::path` representation. Narrow arguments and child environment names/values use UTF-8 at the Win32 conversion boundary. Embedded nulls and otherwise invalid process text remain rejected through the existing #54 infrastructure contract.

`environmentOverrides` contains `Types::Process::EnvironmentOverride` values. An engaged value sets the child variable; `std::nullopt` removes it. Overrides are applied in vector order and later duplicate names win.

## Output capture is bytes

When capture is enabled, stdout and stderr share one capture pipe. `Types::Process::Result::outputBytes` contains retained raw bytes exactly as produced by the child; TestSupport does not validate, normalize, decode, or perform newline conversion on captured output.

At most `maxCapturedOutputBytes` bytes are retained. The pipe continues draining after that limit to prevent a verbose child from blocking. `outputTruncated` means bytes were discarded because of the retention limit, not that capture failed.

A capture infrastructure failure may still preserve useful partial `outputBytes` and a child outcome observed before the failure.

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

`runChildProcess()` remains `noexcept`; expected implementation allocation, setup, launch, capture, wait, inspection, and cleanup failures are returned through `status`. Timeout continues to be a successful domain outcome when enforcement succeeds.

The Win32 backend still owns the job/pipe/handle implementation and preserves the #54 bounded-capture and descendant-cleanup behavior.

@ref test_support_public_api
@ref test_support_examples
