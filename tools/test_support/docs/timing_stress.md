@page test_support_timing_stress TestSupport timing and stress helpers

## Timer

`Timer` measures wall-clock elapsed time:

```cpp
GameWIP::TestSupport::Timer timer;
runScenario();
context.metric("scenarioMs=" + std::to_string(timer.elapsedMilliseconds()));
```

`Timer` is intentionally limited to diagnostic elapsed-time reporting for test suites and sections. Use Google Benchmark for per-iteration timing, calibration, repetitions, and statistical output. See @ref project_benchmarking.

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
