@page logger Logger

Logger is the process-wide runtime diagnostics library. It provides asynchronous, filterable normal logs and a synchronous report path for diagnostics that must bypass queue pressure and runtime filtering.

## Consumer manual

- @subpage logger_quick_start
- @subpage logger_public_api
- @subpage logger_configuration
- @subpage logger_abi
- @subpage logger_lifecycle
- @subpage logger_messages_sources
- @subpage logger_output
- @subpage logger_threading_performance
- @subpage logger_macros
- @subpage logger_reports
- @subpage logger_stats
- @subpage logger_examples
- @subpage logger_troubleshooting

## Maintainer validation

- @subpage logger_testing
- @subpage logger_test_hooks

## Generated API reference

Use @ref GameWIP::Logger for lifecycle, filtering, logging, reporting, statistics, and helper functions. Use @ref GameWIP::Logger::Types for configuration and passive value types. Include `logger/logger_macros.h` only for the optional global `LOGGER_*` shortcuts.

The generated header reference owns exact declarations and overload signatures. The manual pages explain selection rules, runtime behavior, failure semantics, ownership, threading, output, packaging, and caveats.

## Key behavior

Normal log calls are runtime-filtered, copy source and message text before returning, and deliver accepted records through a bounded asynchronous queue. Formatted overloads perform their formatting on the caller thread. Synchronous reports bypass runtime filters and queue pressure; timed report overloads additionally attempt a bounded drain of older queued records.

Logger owns one process-wide runtime. Normal output, platform debugger output, and fatal-popup behavior are separate channels. Queue pressure can drop normal records, including `Error` and `Fatal` records at the hard limit; report APIs are the delivery path for diagnostics that must bypass that pressure.

## Dependency boundary

The public C++ headers are `logger/logger.h` and the optional `logger/logger_macros.h`. Installed consumers link `GameWIP::Logger`; source-tree consumers may link `Logger`.

Logger owns message formatting, source registration, runtime filtering, queueing, normal sinks, synchronous reports, statistics, and platform diagnostic output. Terminal is part of the installed package dependency boundary. FileSystem and IO support Logger internally and are not direct Logger consumer APIs. Assert consumes Logger's report path but owns assertion policy, debugger decisions, and assertion-specific UI.
