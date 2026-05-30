@page logger_testing Logger testing

Logger validation is split into public API tests, stress tests, child-process crash tests, hook-forced rare-path tests, manual UI tests, and performance metrics.

## Normal hook-enabled test build

```powershell
cmake -S . -B build-optimized-debuggable `
  -DASSERT_ENABLED=ON `
  -DASSERT_CHECKS_ENABLED=ON `
  -DGAMEWIP_ENABLE_LOGGER_TEST_HOOKS=ON `
  -DGAMEWIP_ENABLE_ASSERT_TEST_HOOKS=ON `
  -DGAMEWIP_ENABLE_COVERAGE=OFF `
  -DGAMEWIP_BUILD_DOCS=OFF

cmake --build build-optimized-debuggable
.\build-optimized-debuggable\GameWIP.exe
```

Runtime test selection stays in `TestRunOptions` in `game/main.cpp`. CMake controls build features only.

## Stress and performance tests

Stress tests cover multi-producer logging, queue pressure, flush while producers are active, shutdown while producers are active, and repeated init/shutdown. Performance metrics are informational until deliberate baselines are added. Do not use coverage or hook-enabled instrumentation as a performance baseline.

## Test hooks

When `GAMEWIP_ENABLE_LOGGER_TEST_HOOKS=ON`, internal hooks can force rare paths such as file-open failure, file-write failure, file-flush failure, fatal-popup failure, and timed-flush timeout.

Hook headers live under `tools/logger/internal/`, are compile-time gated, and are not production public API.

## Coverage

Coverage is a build feature controlled by `GAMEWIP_ENABLE_COVERAGE=ON`.

```powershell
cmake -S . -B build-coverage `
  -DASSERT_ENABLED=ON `
  -DASSERT_CHECKS_ENABLED=ON `
  -DGAMEWIP_ENABLE_LOGGER_TEST_HOOKS=ON `
  -DGAMEWIP_ENABLE_ASSERT_TEST_HOOKS=ON `
  -DGAMEWIP_ENABLE_COVERAGE=ON

cmake --build build-coverage
.\build-coverage\GameWIP.exe
cmake --build build-coverage --target coverage
```

If `gcovr` is available, the target writes `${build-dir}/coverage/index.html` and `${build-dir}/coverage/coverage.xml` such as `build/coverage/index.html` when the build directory is named `build`. If it is missing, the `coverage` target fails with a message telling you to run `python -m pip install gcovr`.

## Manual UI

The logger fatal popup is manually validated through a report path that requests `ReportPopup::Fatal`. Manual UI tests must remain gated by runtime options and should run near the end of the test suite.
