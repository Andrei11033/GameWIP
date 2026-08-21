@page logger Logger

Logger is the process-wide runtime diagnostics library. Normal logging is asynchronous and filterable; reports are a synchronous emergency path that bypasses queue pressure and runtime filtering.

## How the library is organized

Initialize Logger once with the sinks, queue limits, formatting, and filters the
process needs. Normal log calls copy accepted data into a bounded queue and
return; a worker formats and writes those records. Lifecycle calls such as
`flush()` and `shutdown()` synchronize with that worker. Reports take a separate
synchronous path for diagnostics that must be attempted immediately. Direct
operation failures are returned to the caller, while later asynchronous sink
failures are retained in health state.

## Consumer manual

- @subpage logger_quick_start — Include, link, initialize, write, flush, inspect
  failures, and shut down Logger.
- @subpage logger_public_api — Find lifecycle, logging, reporting, filtering,
  health, statistics, and configuration symbols.
- @subpage logger_configuration — Select sinks, queue limits, formatting,
  filtering, and effective runtime limits.
- @subpage logger_abi — Understand the shared-library, export, package, and
  runtime boundary.
- @subpage logger_lifecycle — Follow initialization, reconfiguration, flush,
  shutdown, and concurrent-call behavior.
- @subpage logger_messages_sources — Understand message ownership, source
  metadata, UTF-8 validation, truncation, and filtering.
- @subpage logger_output — Understand console/file output and sink failure.
- @subpage logger_threading_performance — Understand queue pressure, blocking,
  ordering, worker behavior, allocation, and throughput tradeoffs.
- @subpage logger_macros — Use lazy source-aware macros without evaluating
  filtered message expressions.
- @subpage logger_reports — Use the synchronous emergency path and understand
  how it differs from flushing queued logs.
- @subpage logger_stats — Interpret counters, drops, truncation, and health.
- @subpage logger_examples — See normal logging, filtering, reports, health,
  and shutdown in context.
- @subpage logger_troubleshooting — Diagnose startup, queue, sink, encoding,
  flush, and shutdown problems.

## Maintainer validation

- @subpage logger_testing — See automated, package, ABI, stress, and failure
  coverage.
- @subpage logger_test_hooks — Understand source-tree-only queue and sink fault
  controls.

## Generated API reference

Use @ref GameWIP::Logger for operations. Configuration, result, statistics, and
health types are listed under that namespace in the generated class index.

## Key behavior

Normal log calls copy retained source/message text before return and place accepted records in the bounded worker queue. Reports synchronously attempt the requested diagnostic through every enabled emergency channel and flush active normal sinks. Reports never drain older asynchronous records; callers that need backlog completion use `flush()` explicitly.

Lifecycle and synchronous operations return direct `IO::Types::Status` or a Logger result containing status plus a distinct domain outcome. Asynchronous sink failures are exposed by `getHealth()` rather than mutable process-wide last-operation state.

## Dependency boundary

Installed consumers link `GameWIP::Logger`. IO is public because Logger's public result surface contains IO status types. Terminal is an installed dependency used for console output; FileSystem and Unicode remain private implementation dependencies.
