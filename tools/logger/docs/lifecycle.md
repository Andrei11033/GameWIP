@page logger_lifecycle Logger lifecycle

Logger owns one process-wide runtime instance. Public lifecycle operations are internally serialized with one another.

## Initialization functions

| API | Use when |
| --- | --- |
| `defaultConfig()` | Start from the general-purpose preset and edit fields. |
| `lowMemoryConfig()` | Prefer lower retained memory. |
| `throughputConfig()` | Prefer larger bursts and retained storage. |
| `init(config)` | Supply the complete configuration. |
| `initDefault()` | Use `defaultConfig()` unchanged. |
| `initConsole(level)` | Start console-only output. |
| `initFile(directory, level)` | Request file-only output, with the default fallback policy. |

`init()` copies retained configuration state before returning. Caller-owned arrays behind `sources`, `sourceFilters`, and `levelFilters`, and text behind their `string_view` fields, need to remain valid only for the duration of the call.

Calling `init()` while a worker is already active returns `AlreadyRunning` and leaves that runtime unchanged.

## Disabled configuration

`Output::None` is a valid configuration. Logger publishes the configuration and filters but does not allocate a queue or start a worker. `init()` may return `Success`, while `isRunning()` remains false because no normal asynchronous sink is active.

`enableDebugOutput` and `enableFatalPopup` are separate channels; their APIs can still be used according to configuration even when normal output is `None`.

## Effective state

| API | Meaning |
| --- | --- |
| `isRunning()` | True while the asynchronous worker accepts normal logs. |
| `getMinLevel()` | Active startup severity floor. |
| `getOutput()` | Effective normal sink mode after fallback. |
| `getLogFilePath()` | UTF-8 active file path, or empty when no file sink is active. |
| `getQueueLimits()` | Effective sanitized queue/message values. |
| `getLastResult()` | Most recent process-wide Logger result. |
| `getLastPlatformError()` | Most recent recorded native-platform failure. |

Runtime filters and visible statistics are reset for each initialization.

## Flush

`flush()` waits for accepted asynchronous work to drain and then flushes active sinks. The unbounded overload can wait indefinitely if producers continue submitting records or an underlying sink does not complete.

`flush(timeout)` computes one absolute deadline before acquiring Logger's lifecycle boundary. Lifecycle, queue, and output serialization plus the drain wait and pre-I/O checks all consume that same duration. It returns true only when the queue drains and observable sink flushing completes by the deadline. Non-positive durations make the attempt immediate.

The bound is best-effort at the native I/O boundary. Once a synchronous console or filesystem flush begins, the operating-system call is not cancellable and can extend the total call beyond the deadline. Logger requests console flushes, but console-flush status is not surfaced; a file-flush failure makes the result false.

Neither overload prevents other threads from attempting new normal logs. Stop producers before a final deterministic flush where possible.

## Shutdown

`shutdown()`:

1. publishes disabled producer state;
2. wakes and joins the worker after accepted work is drained;
3. flushes and closes the file sink;
4. clears filters, source registry, path, and queue state;
5. releases or retains storage according to `releaseStorageOnShutdown`.

Repeated calls and calls before initialization are safe cleanup operations. An `atexit` callback also invokes shutdown, but explicit shutdown is preferred so producer lifetime and teardown ordering remain under application control.

A producer that races shutdown remains memory-safe. Calls made after disabled state is published can be skipped; callers must not expect persistent logging after final shutdown begins.

## Synchronization

- Normal producer logging is thread-safe.
- Lifecycle calls, public flush calls, and synchronous report paths are serialized internally through the lifecycle boundary.
- Filter updates and queries are thread-safe.
- State and statistics getters are snapshots; they are not transactions with concurrent logging.

## Related pages

- @ref logger_configuration
- @ref logger_threading_performance
- @ref logger_reports
- @ref logger_troubleshooting
