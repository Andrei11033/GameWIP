@page assert_testing Assert testing

Assert validation is split into macro contract tests, child-process crash tests, automated interactive tests, manual UI tests, hook-forced rare-path tests, stress tests, and performance metrics.

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

Runtime test selection stays in `TestRunOptions` in `game/main.cpp`.

## Child-process tests

Fatal paths such as failed `ASSERT`, `UNREACHABLE`, `DEBUG_BREAK`, interactive Abort, and interactive Break are validated in child processes so the main test runner can continue.

## Automated interactive tests

Automated interactive tests use `GAMEWIP_ASSERT_TEST_ACTION` and must not open real popups. These tests cover Break, Abort, Ignore Once, Always Ignore, popup suppression, and side-effect behavior.

## Manual UI tests

Manual UI tests open real Windows UI. They are allowed to block and should run only when runtime options enable them.

Manual UI coverage includes Ignore Once, Always Ignore, Break with debugger attached, Abort in a child process, TaskDialog behavior, and MessageBox fallback behavior where practical.

## Test hooks

When `GAMEWIP_ENABLE_ASSERT_TEST_HOOKS=ON`, internal hooks can force TaskDialog fallback, MessageBox failure/default behavior, debugger-attached override paths, and popup suppression paths. Hook headers live under `tools/debug/assert/internal/`, are compile-time gated, and are not production public API.

## Coverage

Coverage is controlled by `GAMEWIP_ENABLE_COVERAGE=ON`, not by runtime test options. Run the full hook-enabled suite before building the `coverage` target.
