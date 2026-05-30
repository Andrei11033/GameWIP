@page logger_stats Logger statistics

Logger statistics describe queue activity, written output, diagnostics, and memory behavior.

Important rules:

- `queued` counts accepted asynchronous log entries.
- `written` counts entries accepted by at least one sink.
- `queueDropsSoft` and `queueDropsHard` only describe queue pressure.
- Filtered logs are not drops.
- `formatFailures`, `fileWriteFailures`, `unknownSourceUses`, and `truncated` are diagnostic counters.
- `resetStats()` clears resettable counters but preserves lifetime queue-drop accounting.

## Queue counters

`queued` counts accepted async log entries. `written` counts entries or synchronous reports accepted by at least one sink. `queueDropsSoft` and `queueDropsHard` describe queue pressure only; filtered logs are not drops.

`getLifetimeDroppedLogCount()` preserves the lifetime drop total. `resetStats()` clears resettable stats but does not erase the lifetime drop value used by shutdown reporting.

## Diagnostics

`unknownSourceUses` increments when a queued entry or report uses an unregistered `SourceId`. `truncated` increments when message storage applies the truncation policy. Formatting, allocation, file write, file flush, debug output, and fatal popup failures are surfaced through dedicated counters where practical.

## Memory stats

`getMemoryStats()` reports logger-owned queue/message storage and platform process memory when available. Treat process memory values as a platform snapshot, not as logger-only allocations.

The test suite contains direct tests for the public counter meanings.
