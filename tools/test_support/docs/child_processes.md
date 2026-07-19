@page test_support_child_processes Child processes

Child execution is configured through `Types::ChildProcessOptions`, returns `Types::ChildProcessResult`, and is implemented by the Win32 backend.

## Launch model

The executable is launched directly; no shell is involved. Shell expansion, pipelines, quoting syntax from a shell, and redirection operators are not interpreted. TestSupport quotes the argument vector according to Windows command-line rules.

The child inherits the parent's working directory. The API does not provide a child working-directory option or stdin payload.

`executablePath` uses native `std::filesystem::path` representation. Narrow arguments and environment names/values are UTF-8. Embedded nulls are rejected. Environment names must be non-empty and contain no `=`.

## Environment block

When `inheritParentEnvironment` is true, the parent block is copied before overrides. Otherwise the child starts from an otherwise empty block. Overrides are applied in vector order; later duplicates win. Name matching is case-insensitive on Windows. `std::nullopt` removes a key, while an engaged empty string creates an explicitly empty child value.

## Standard handles and capture

When capture is enabled, child stdout and stderr share one pipe. They cannot be separated afterward; retained byte order reflects the actual writes observed by the pipe.

`output` stores raw bytes in `std::string`. TestSupport does not validate UTF-8 or perform newline conversion. At most `maxCapturedOutputBytes` are retained, while excess bytes continue to be drained and are discarded. `outputTruncated` is true only when bytes were actually discarded. A zero limit with a silent child is not truncated.

When capture is disabled, the child receives inheritable duplicates of the selected parent standard handles, `output` stays empty, and the retained limit is ignored. Other inheritable parent handles are excluded through an explicit handle allowlist.

## Timeout and process tree

A negative timeout waits indefinitely. Zero performs an immediate wait. Durations above the largest finite Win32 wait are clamped.

Timeout controls when termination begins; it is not a strict upper bound on total `runChildProcess()` duration. TestSupport still completes job termination, process inspection, and output-reader shutdown before returning.

The process is assigned to a Win32 job before it is resumed. Timeout and infrastructure failures terminate the job. Remaining descendants are also terminated after the primary process completes so descendants cannot retain capture handles indefinitely.

`wasTerminatedByTest` describes TestSupport's primary-process termination handling. It does not enumerate every descendant terminated by the job.

## Result interpretation

Inspect fields together:

1. `timedOut` says whether the configured wait expired.
2. `wasTerminatedByTest` says TestSupport requested termination during timeout or infrastructure-failure handling.
3. `infrastructureFailure` distinguishes launch, setup, wait, inspection, or capture failure from a child result.
4. `exitCode` is the complete native unsigned 32-bit code when `infrastructureFailure` is false.

Infrastructure failure leaves `exitCode` at zero; inspect `infrastructureFailure` rather than using a sentinel. Native values from `0` through `0xffffffff` are represented without narrowing or collision.

The exit code following test-requested termination is not a portable child result. `exitedSuccessfully()` means zero exit with no infrastructure failure, timeout, or test termination. `exitedWithFailure()` is true for infrastructure failure, any nonzero exit, timeout, or test termination.

## Exceptions

Invalid process text throws `std::invalid_argument`. Oversized UTF-8 conversion input throws `std::length_error`. Unexpected text-conversion failure throws `std::runtime_error`. Standard allocation/path exceptions may also propagate.

Platform launch and wait failures normally return `infrastructureFailure == true` rather than throwing.

## Related pages

- @ref test_support_public_api
- @ref test_support_examples
- @ref test_support_troubleshooting
