@page logger_test_hooks Test hooks

Logger validation hooks are available only when `LOGGER_INTERNAL_TEST_HOOKS` is enabled. They provide one-shot file write/flush failures,
fatal-popup failure, deterministic timed-flush timeout, and worker-delivery coordination pauses.

`forceNextTimedFlushTimeout()` targets the next finite `Logger::flush(timeout)` operation. Expect successful IO status and
`Types::FlushResult::outcome == Types::FlushOutcome::TimedOut`.

Hooks are validation-only and are not part of installed consumer behavior.
