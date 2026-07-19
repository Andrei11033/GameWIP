@page logger_test_hooks Test hooks

@warning These are source-tree maintainer interfaces. They are not installed, versioned, or supported for consumer/game runtime use.

## Availability

`LOGGER_ENABLE_TEST_HOOKS=ON` builds the Logger runtime and approved source-tree targets with `INTERNAL_LOGGER_TEST_HOOKS=1`. The repository validation preset enables the hooks through its test configuration.

## Include

```cpp
#include "logger/internal/logger_test_hooks.h"
```

The including target must link the source-tree `Logger` target and receive the matching compile definition. Installed packages intentionally do not provide this header.

## Reset rule

Call `GameWIP::Logger::TestHooks::reset()` before and after every hook-driven scenario. Cleanup must run even when an expectation fails so one-shot flags or paused coordination state cannot leak into another test.

## Failure-injection hooks

| API | Effect |
| --- | --- |
| `forceNextFileOpenFailure()` | Makes the next Logger file-open attempt fail. |
| `forceNextFileWriteFailure()` | Makes the next file write fail. |
| `forceNextFileFlushFailure()` | Makes the next file flush fail. |
| `forceNextQueueAllocationFailure()` | Makes the next queue-entry copy behave like allocation failure; ordered publication continues through a skip marker. |
| `forceNextFatalPopupFailure()` | Makes the next logger-owned popup attempt report a platform failure. |
| `forceNextTimedFlushTimeout()` | Makes the next timed `flush(timeout)` bounded wait report timeout. |

These hooks are one-shot.

## Worker-wait coordination

Protocol:

1. `armWorkerWaitPause()`;
2. trigger the worker transition under test;
3. `waitForWorkerWaitPause()`;
4. optionally use `waitForQueuePublication()` to establish the publication milestone;
5. `releaseWorkerWaitPause()`;
6. reset.

The worker retains Logger's coordination mutex while paused. Waiting or release calls used in the wrong order can block indefinitely. Use this protocol only in isolated child-process scenarios with an external timeout.

## Final-producer coordination

Protocol:

1. `armFinalProducerLeavePause()`;
2. run the producer/shutdown scenario;
3. `waitForFinalProducerLeavePause()`;
4. establish the state being validated;
5. `releaseFinalProducerLeavePause()`;
6. reset.

This pauses a producer before the active-producer count is decremented, allowing deterministic validation of final-drain and shutdown wakeup behavior.

## Complete API

- `reset()`
- `forceNextFileOpenFailure()`
- `forceNextFileWriteFailure()`
- `forceNextFileFlushFailure()`
- `forceNextQueueAllocationFailure()`
- `forceNextFatalPopupFailure()`
- `forceNextTimedFlushTimeout()`
- `armWorkerWaitPause()`
- `waitForWorkerWaitPause()`
- `waitForQueuePublication()`
- `releaseWorkerWaitPause()`
- `armFinalProducerLeavePause()`
- `waitForFinalProducerLeavePause()`
- `releaseFinalProducerLeavePause()`

## Example

```cpp
#include "logger/internal/logger_test_hooks.h"
#include "logger/logger.h"

#include <chrono>

int main()
{
    using namespace GameWIP::Logger;

    static_cast<void>(initConsole());

    TestHooks::reset();
    TestHooks::forceNextTimedFlushTimeout();
    const bool flushed = flush(std::chrono::milliseconds{50});
    TestHooks::reset();

    shutdown();
    return flushed ? 1 : 0;
}
```

## Related pages

- @ref logger_testing
- @ref logger_stats
- @ref logger_troubleshooting
