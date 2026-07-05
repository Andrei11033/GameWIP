@page logger_threading_performance Logger threading and performance

Normal logging is designed for producer threads. The worker thread owns sink writes while the logger is running.

## Producer path expectations

- Logging calls are safe from multiple producer threads.
- Messages are copied before queueing.
- Formatting happens on the caller thread before insertion.
- Registered enum/`SourceId` sources are preferred in hot paths.
- `LOGGER_*` macros can avoid message evaluation when filtered.
- Direct formatted calls may still evaluate expensive arguments before the logger performs its filter check.

Custom formatter callbacks may safely call Logger again. Nested formatting uses separate per-thread scratch storage, so it cannot overwrite the outer message. The nested record is queued before the outer record because the nested call completes while the outer message is still being formatted.

## Worker path expectations

- File/console writes are performed by the worker thread for normal queued logs.
- File output retains one FileSystem writer and preserves worker batching.
- Each console record is one shared Terminal operation; Terminal owns process-wide per-stream serialization.
- The worker drains batches and sleeps/wakes based on queue work.
- `flush()` waits for accepted queued work and sink flushing.
- `shutdown()` disables normal acceptance before draining accepted work.

## Report path

Reports are intentionally synchronous. They may block, flush, and use UI behavior. Do not use reports in frame hot paths.

`fatalTerminate(...)` is also synchronous and terminates. Treat it as a failure-path API, not a logging primitive.

## Related pages

- @ref logger_macros
- @ref logger_stats
- @ref logger_reports
