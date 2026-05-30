@page logger_stats Logger statistics

`Logger::getStats()` returns resettable diagnostic counters. `Logger::getLifetimeDroppedLogCount()` returns lifetime queue-drop accounting that survives `resetStats()`.

## Counter meanings

- `queued`: async log entries accepted into the queue.
- `written`: entries accepted by at least one active sink.
- `queueDropsSoft`: low-priority async entries dropped at the soft queue limit.
- `queueDropsHard`: async entries dropped at the hard queue limit.
- `allocationFailures`: allocation or internal formatting-storage failures caught by the logger.
- `fileWriteFailures`: file write or flush failures.
- `unknownSourceUses`: written entries that used an unregistered `SourceId` fallback.
- `formatFailures`: runtime formatting failures.
- `truncated`: messages shortened to the configured maximum length.
- `peakQueueDepth`: largest observed queued depth since the last stats reset.

## Important rules

- Filtered normal logs are not drops.
- Reports bypass the queue and therefore do not increment `queued`.
- A report can increment `written`, `truncated`, `fileWriteFailures`, or `unknownSourceUses` depending on what happened.
- `resetStats()` clears resettable counters but preserves lifetime drop accounting.

## Memory stats

`Logger::getMemoryStats()` returns best-effort memory information. Some fields are based on retained logger storage, while process memory depends on the platform backend. Treat memory stats as diagnostics rather than precise allocator accounting.
