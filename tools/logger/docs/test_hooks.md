@page logger_test_hooks Test hooks

Logger validation hooks are available only when `INTERNAL_LOGGER_TEST_HOOKS` is enabled. They provide one-shot file open/write/flush failures, queue-copy allocation failure, fatal-popup failure, deterministic timed-flush timeout, and lifecycle/worker coordination pauses.

`forceNextTimedFlushTimeout()` targets the next finite `Logger::flush(timeout)` operation and should now be asserted through `FlushResult::outcome == FlushOutcome::TimedOut` with successful IO status.

Hooks are validation-only and are not part of installed consumer behavior.
