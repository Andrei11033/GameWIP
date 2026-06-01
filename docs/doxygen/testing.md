@page gamewip_testing Project testing policy

GameWIP separates build-time features from runtime test selection.

## Rule

CMake controls compile-time/build features:

- assertions/checks enabled state,
- test-hook compilation,
- coverage instrumentation,
- Doxygen generation.

`TestRunOptions` in `game/main.cpp` controls what the test executable actually runs:

- logger tests,
- assert tests,
- stress tests,
- child-process crash tests,
- performance metrics,
- automated interactive tests,
- manual UI tests,
- popup tests,
- iteration counts and thread counts,
- report output paths.

## Hook-enabled test build

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

## Focused TestSupport library run

```powershell
.\build\GameWIP.exe --test-support-only
```

Use `--no-test-support-child-process` with `--test-support-only` when intentionally skipping child-process coverage while debugging unrelated TestSupport behavior.

Use `--test-support-manual` for the focused TestSupport manual prompt checks. This mode runs TestSupport only, enables manual TestSupport checks, and leaves Logger/Assert manual UI tests out of the run.

## Manual UI tests

Manual UI tests are intentionally blocking and must remain opt-in through runtime options. Automated interactive assert tests must use `GAMEWIP_ASSERT_TEST_ACTION` and must not open real popups.

## Package and docs validation

Package/install validation is done through normal install commands and static inspection of generated package config files. Do not add package smoke tests unless that scope is explicitly requested.

Doxygen generation is also manual validation. The docs target is opt-in and should be run only when intentionally checking generated documentation.
