@page logger_test_hooks Logger test hooks

Logger test hooks are advanced testing-only features. They are not production API and are not installed as normal public headers.

## Enabling

```powershell
-DGAMEWIP_ENABLE_LOGGER_TEST_HOOKS=ON
```

This maps to the library-local `LOGGER_TEST_HOOKS` option and exports `GAMEWIP_LOGGER_TEST_HOOKS=1` to test code.

## Purpose

Hooks make rare paths deterministic:

- force the next file flush failure,
- force the next file write failure,
- force the next fatal popup failure,
- force a timed flush timeout,
- reset hook state after each scenario.

## Rules

- Hook namespaces exist only when `GAMEWIP_LOGGER_TEST_HOOKS=1`.
- Hook headers live under `tools/logger/internal/`.
- Hook headers are excluded from normal installs.
- One-shot hooks use `forceNext...` naming.
- Persistent hooks should use `set...Override` / `clear...Override` naming.
- Tests must reset hooks so forced state cannot leak into later scenarios.

## Installed packages

Installed Logger packages intentionally do not expose internal hook headers as normal public API. Build hook-enabled tests from the source tree.
