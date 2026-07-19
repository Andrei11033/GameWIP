@page logger_stats Statistics

`getStats()` returns a relaxed snapshot of resettable counters. Fields can reflect slightly different instants under concurrent activity; use them for diagnostics and tests, not transactional application state.

## `Stats` fields

| Field | Meaning |
| --- | --- |
| `queued` | Normal records accepted into the asynchronous queue. |
| `written` | Queued records or synchronous reports accepted by at least one normal console/file sink. |
| `queueDropsSoft` | `Trace` through `Warn` records refused at soft pressure. |
| `queueDropsHard` | Records of any severity refused at the hard limit. |
| `allocationFailures` | Logger-internal allocation, copy, or formatting-storage failures. |
| `fileWriteFailures` | File write or flush failures while other paths continue where possible. |
| `unknownSourceUses` | Written queued entries or reports that resolved an unregistered `SourceId`. |
| `formatFailures` | Formatting operations rejected by `std::format`/`std::vformat`, including runtime format errors and formatter-reported `std::format_error`. |
| `truncated` | Written messages retained with the truncation policy. |
| `peakQueueDepth` | Highest observed logical queue depth since initialization or reset. |

Filtered calls are not queue drops. `queued` can exceed `written` without drops because the worker rechecks runtime filters and may suppress an already queued record after a concurrent filter change.

Debugger-only output and popup success do not increment `written`; that counter describes normal console/file sinks.

## Lifetime drops and reset

`getLifetimeDroppedLogCount()` is the soft-plus-hard drop total since the active initialization. `resetStats()` clears visible resettable counters but preserves this lifetime value for shutdown diagnostics.

When reset occurs during activity, `peakQueueDepth` restarts from the current logical queue depth rather than necessarily from zero.

## `MemoryStats` fields

| Field | Meaning |
| --- | --- |
| `loggerRetainedBytes` | Best-effort total of fixed Logger state and reported retained capacities. |
| `queueStorageBytes` | Ring-slot and worker-batch vector storage. |
| `messageArenaBytes` | Preallocated ring and worker-batch message arenas. |
| `sourceRegistryBytes` | Current published source-registry retained storage. |
| `entryTextHeapCapacityBytes` | Heap fallback capacity retained by idle entry text, when safely inspectable. |
| `entryTextHeapCapacityAvailable` | Whether that entry-capacity inspection avoided racing producers/worker activity. |
| `processWorkingSetBytes` | Whole-process OS working-set snapshot. |
| `processPrivateBytes` | Whole-process OS private-memory snapshot. |
| `processMemoryAvailable` | Whether the platform query succeeded. |

Logger-owned values describe retained capacities and omit allocator overhead, worker-local scratch, and producer thread-local formatting scratch. Process values describe the entire process, not Logger alone.

## Diagnostic result state

`getLastResult()` and `getLastPlatformError()` are mutable process-wide snapshots. File failures, debugger output, popup, and time conversion can replace platform error state. Capture them near the operation being diagnosed.

## Related pages

- @ref logger_threading_performance
- @ref logger_configuration
- @ref logger_output
