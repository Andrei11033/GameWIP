@page logger Logger

Logger is the process-wide runtime diagnostics library for filtered logs and synchronous reports.

## How the library is organized

Configure sinks, queue limits, formatting, and filters during initialization.
A worker writes accepted records; lifecycle calls coordinate with that worker.

## Key behavior

Normal calls copy accepted data into a bounded queue and return. Reports bypass
filters and queue pressure, try enabled channels, and flush active normal sinks
without draining older queued records. Fallible operations return shared
status-bearing results; asynchronous sink failures appear in health state.

## Consumer manual

- @subpage logger_quick_start - Include, link, initialize, write, flush, inspect
  failures, and shut down Logger.
- @subpage logger_public_api - Find lifecycle, logging, reporting, filtering,
  health, statistics, and configuration symbols.
- @subpage logger_configuration - Select sinks, queue limits, formatting,
  filtering, and effective runtime limits.
- @subpage logger_abi - Understand the shared-library, export, package, and
  runtime boundary.
- @subpage logger_lifecycle - Follow initialization, reconfiguration, flush,
  shutdown, and concurrent-call behavior.
- @subpage logger_messages_sources - Understand message ownership, source
  metadata, UTF-8 validation, truncation, and filtering.
- @subpage logger_output - Understand console/file output and sink failure.
- @subpage logger_threading_performance - Understand queue pressure, blocking,
  ordering, worker behavior, allocation, and throughput tradeoffs.
- @subpage logger_macros - Use lazy source-aware macros without evaluating
  filtered message expressions.
- @subpage logger_reports - Use the synchronous emergency path and understand
  how it differs from flushing queued logs.
- @subpage logger_stats - Interpret counters, drops, truncation, and health.
- @subpage logger_examples - See normal logging, filtering, reports, health,
  and shutdown in context.
- @subpage logger_troubleshooting - Diagnose startup, queue, sink, encoding,
  flush, and shutdown problems.

## Maintainer validation

- @subpage logger_testing - See automated, package, ABI, stress, and failure
  coverage.
- @subpage logger_test_hooks - Understand source-tree-only queue and sink fault
  controls.

## Generated API reference

Use @ref GameWIP::Logger for operations. Configuration, result, statistics, and
health types are listed under that namespace in the generated class index.

## Dependency boundary

Installed consumers link `GameWIP::Logger`. Status is public because Logger's
result types use the shared contract. IO, Terminal, FileSystem, and Unicode are
private implementation dependencies.
