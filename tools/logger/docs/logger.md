@page logger Logger

Logger is the process-wide runtime diagnostics library. Normal logging is asynchronous and filterable; reports are a synchronous emergency path that bypasses queue pressure and runtime filtering.

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

## Key behavior

Normal log calls copy retained source/message text before return and place accepted records in the bounded worker queue. Reports synchronously attempt the requested diagnostic through every enabled emergency channel and flush active normal sinks. Reports never drain older asynchronous records; callers that need backlog completion use `flush()` explicitly.

Lifecycle and synchronous operations return direct `IO::Types::Status` or a Logger result containing status plus a distinct domain outcome. Asynchronous sink failures are exposed by `getHealth()` rather than mutable process-wide last-operation state.

## Dependency boundary

Installed consumers link `GameWIP::Logger`. IO is public because Logger's public result surface contains IO status types. Terminal is an installed dependency used for console output; FileSystem and Unicode remain private implementation dependencies.
