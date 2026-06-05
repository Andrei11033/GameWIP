@page test_support_timing_stress TestSupport timing and stress helpers

## Timer

`Timer` measures wall-clock elapsed time:

```cpp
GameWIP::TestSupport::Timer timer;
runScenario();
context.metric("scenarioMs=" + std::to_string(timer.elapsedMilliseconds()));
```

`nanosecondsPerIteration(iterations)` converts elapsed time to average nanoseconds per iteration. It returns zero for zero iterations.

## IterationMetric

`Types::IterationMetric` is a passive value for named iteration measurements. Use it when you want to store the metric before formatting a report line.

Performance metrics are informational. TestSupport does not apply hard thresholds by default.

## StartGate

`StartGate` lets worker threads wait until the main test releases them:

```cpp
GameWIP::TestSupport::StartGate gate;

std::thread worker([&]
{
    gate.wait();
    runWorker();
});

gate.open();
worker.join();
```

## StopFlag

`StopFlag` is a tiny atomic cooperative-stop helper:

```cpp
GameWIP::TestSupport::StopFlag stop;

while (!stop.stopRequested())
{
    runOneStep();
}
```

## runWorkers

`runWorkers(workerCount, workerFunction)` starts `workerCount` threads, gives each worker its own callable copy, joins all workers, and rethrows the first captured worker exception.

The callable may accept either no arguments or a `std::size_t` worker index:

```cpp
GameWIP::TestSupport::runWorkers(4, [](std::size_t workerIndex)
{
    runWorker(workerIndex);
});
```

Use these helpers for simple stress patterns. Keep domain-specific checks in the relevant test file instead of adding Logger-specific, Assert-specific, or engine-specific logic to TestSupport.
