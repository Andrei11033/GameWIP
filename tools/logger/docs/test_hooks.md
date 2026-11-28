@page logger_test_hooks Test hooks

Logger validation hooks are available only when `LOGGER_INTERNAL_TEST_HOOKS` is enabled. They provide one-shot file-write, file-flush, and fatal-popup
failures, forced timeouts for finite flushes and reports, and worker-delivery coordination pauses.

`forceNextTimedFlushTimeout()` targets the next finite `Logger::flush(timeout)` or timed report operation. It sets the result outcome to `TimedOut`;
the status still reports any operation failure.

Hooks are validation-only and are not part of installed consumer behavior.
