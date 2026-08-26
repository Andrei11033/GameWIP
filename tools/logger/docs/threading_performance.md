@page logger_threading_performance Threading and performance

Normal logging is designed for concurrent producer threads. Formatting and message construction occur on the producer; sink I/O occurs on the worker.

## Producer path

- Runtime state and filters are checked before queue reservation.
- String and message bytes are copied before the call returns.
- Accepted records reserve ordered slots in a bounded multi-producer/single-consumer ring.
- Queue entry allocation/copy failure publishes an internal skip marker so later producer tickets remain drainable in order.
- `LOGGER_*` or explicit `shouldLog()` guards can avoid expensive message argument evaluation.
- Direct formatted calls cannot avoid C++ argument evaluation that occurs before the function call.

Registered enum/`SourceId` sources reduce per-entry source storage and support source filtering. String sources are appropriate when dynamic source
labels are needed.

## Queue pressure

`maxQueueSize` is the soft depth. At or above it, `Trace`, `Debug`, `Info`, and `Warn` may be refused and counted in `queueDropsSoft`.

The hard depth is derived from `hardQueueMultiplier`. At or above it, every severity—including `Error` and normal `Fatal`—may be refused and counted
in `queueDropsHard`.

Queue drops apply only to normal asynchronous logs. Synchronous reports bypass the queue. Use reports for failure paths that must not depend on queue
capacity.

## Worker path

The worker drains entries in reservation order, writes console records, batches file records, and flushes according to configuration and explicit
synchronization requests.

Runtime filters are checked again on the worker. A record accepted before a filter update can therefore be suppressed before reaching a sink.
Consequently, `queued` can exceed `written` even when no queue-drop counter changes.

## Thread safety and ordering

- Concurrent normal logging is supported.
- Records are drained by queue ticket order, but wall-clock call completion across producers is not a total ordering guarantee.
- Source/level filter updates are thread-safe.
- Lifecycle operations and synchronous reports are serialized internally.
- Reports write immediately relative to their calling thread but do not stop other producers.
- `flush()` waits on accepted queue state; it does not prevent new producers from submitting work.

## Formatting memory

Formatted overloads reuse nesting-safe per-thread scratch storage. `StrictBounded` constrains formatting growth; `FastNormal` can retain a large
thread-local capacity after an unusually large message.

`inlineMessageCapacity` trades fixed queue storage for fewer heap fallbacks. When `releaseMessageMemoryAfterWrite` is true, Logger also releases
oversized queue-entry text, worker line/batch scratch, and producer formatting scratch that grew beyond the active message limit.
`releaseStorageOnShutdown` trades idle memory for reallocation on later initialization.

Use `getMemoryStats()` for diagnostic retained-capacity snapshots, not allocator-precise accounting.

## Blocking paths

Normal producer calls do not perform normal console/file I/O, but they can spin/yield while waiting for an already reserved ring slot to become
reusable. Reports, flushes, shutdown, popup paths, and file setup are synchronous and can block.

## Related pages

- @ref logger_configuration
- @ref logger_messages_sources
- @ref logger_macros
- @ref logger_reports
- @ref logger_stats
