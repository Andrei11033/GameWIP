@page logger_output Output channels

Logger has four distinct output channels: normal console output, normal file output, platform debugger output, and the fatal popup. Normal sink selection does not automatically enable or disable the latter two channels.

## Record layout

Normal records and synchronous reports use:

```text
[HH:MM:SS][LEVEL][SOURCE]: message
```

Timestamps use local process time. `LEVEL` is `TRACE`, `DEBUG`, `INFO`, `WARN`, `ERROR`, or `FATAL`. Unknown registered IDs use `UnknownSource`.

## Console sink

- `Trace`, `Debug`, `Info`, and `Warn` route to stdout.
- `Error` and `Fatal` route to stderr.
- Styling uses the shared Terminal runtime when `enableConsoleColor` is true.
- Terminal's automatic style policy omits escape sequences for unsupported or redirected streams.
- Each record uses the native console line ending.
- `flushConsoleEveryWrite` requests a data flush after each console record.

Logger and direct Terminal users share Terminal's process-wide stream coordination; avoid bypassing Terminal with unrelated unsynchronized standard-stream writes when line integrity matters.

## File sink

File output creates the requested directory and opens a collision-safe file named from local date/time, adding a numeric suffix when needed. `getLogFilePath()` returns the selected path as UTF-8.

The active file is created exclusively for writing while allowing other processes to read it. File contents use UTF-8 message bytes and `\n` line endings. Worker records are batched. `flushFileEveryBatch` flushes after each batch; explicit `flush()`, reports, and shutdown also flush as defined by their contracts.

A file write or flush failure increments `fileWriteFailures`, records a file platform error, and allows other configured channels to continue. Logger does not automatically reopen a failed file sink during the same initialization.

## File setup fallback

When file setup fails:

- `Both` retains console output;
- file-only output falls back to console when `fallbackToConsoleOnFileFailure` is true;
- otherwise the effective normal output becomes `None`.

The initialization result preserves the file failure. Inspect `getOutput()`, `getLogFilePath()`, and `getLastPlatformError()`.

## `Output::None`

`Output::None` disables normal console/file logging and does not start the asynchronous worker. It does not override `enableDebugOutput` or `enableFatalPopup`.

## Platform debugger output

`writeDebugOutput()` bypasses normal sinks, queueing, and runtime filters. It emits one formatted line only when `enableDebugOutput` is true.

Synchronous reports also mirror their diagnostic line to this channel when enabled. Debugger-output failures update platform diagnostic state but do not make a normal sink write successful.

## Fatal popup

The logger-owned popup is attempted only by report/fatal-terminate paths that request fatal popup behavior and only when `enableFatalPopup` is true. It can block the calling thread and is not intended for routine runtime diagnostics or frame paths.

On Windows, Logger converts UTF-8 to Unicode Win32 text. Invalid UTF-8 conversion or popup API failure is recorded through `getLastPlatformError()`.

## Related pages

- @ref logger_configuration
- @ref logger_reports
- @ref logger_stats
- @ref logger_troubleshooting
