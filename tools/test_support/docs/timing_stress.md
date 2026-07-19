@page test_support_timing_stress Timing and stress helpers

## `Timer`

`Timer` uses `std::chrono::steady_clock`, starts at construction, and can be restarted with `reset()`. It is suitable for suite diagnostics and section metrics, not calibrated benchmark assertions. Concurrent reset/read of the same object is not synchronized.

## `StartGate`

`StartGate` is a one-shot gate. `wait()` blocks until `open()` is called. `open()` is idempotent; after opening, current and future waiters pass. The gate cannot be reset.

## `StopFlag`

`StopFlag` is a one-way cooperative stop signal. `requestStop()` performs a release store and `stopRequested()` performs an acquire load. There is no reset operation.

## `runWorkers()`

`runWorkers(workerCount, workerFunction)`:

- starts no threads when `workerCount == 0`;
- requires a copy-constructible callable;
- gives each worker a separate callable-object copy;
- invokes `worker(index)` when the callable accepts `std::size_t`, otherwise invokes `worker()`;
- selects the indexed form when both are viable;
- joins every successfully started thread;
- captures one worker exception and rethrows it after all workers join;
- allows other workers to continue after one throws.

A separate callable object does not imply isolated captured state. References, pointers, and shared objects captured by the callable remain shared and need their own synchronization.

When multiple workers throw concurrently, which exception is retained is scheduling-dependent. Thread creation, vector allocation, or callable copy/move can also throw. If startup fails after some threads were created, TestSupport joins those threads before rethrowing the startup failure.

Design worker coordination so a partial-startup failure cannot strand already-started workers waiting for a participant that was never created. A fixed barrier expecting exactly `workerCount` participants is unsafe unless startup failure has an independent release path.

## Example

```cpp
#include "test_support/test_support.h"

#include <atomic>
#include <cstddef>
#include <thread>

int main()
{
    std::atomic_size_t visits{0};
    GameWIP::TestSupport::StartGate gate;

    std::thread coordinator([&gate] { gate.open(); });
    GameWIP::TestSupport::runWorkers(
        4,
        [&](std::size_t)
        {
            gate.wait();
            visits.fetch_add(1, std::memory_order_relaxed);
        });
    coordinator.join();

    return visits.load(std::memory_order_relaxed) == 4 ? 0 : 1;
}
```

## Related pages

- @ref test_support_examples
- @ref test_support_testing
