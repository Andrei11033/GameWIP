@page assert_test_hooks Assert test hooks

Assert test hooks are advanced testing-only features. They are not production API and are not installed as normal public headers.

## Enabling

```powershell
-DASSERT_TEST_HOOKS=ON
```

This maps to the library-local `ASSERT_TEST_HOOKS` option and exports `INTERNAL_ASSERT_TEST_HOOKS=1` to test code.

## Purpose

Hooks make rare paths deterministic:

- force primary action-dialog fallback,
- force fallback action-dialog/default behavior,
- override debugger-attached checks,
- override popup suppression behavior,
- reset forced state between tests.

## Rules

- Hook namespaces exist only when `INTERNAL_ASSERT_TEST_HOOKS=1`.
- Hook headers live under `tools/debug/assert/internal/`.
- Hook headers are excluded from normal installs.
- Hooks are for tests and coverage, not user code.
- Tests must reset hook state after each forced scenario.

## Installed packages

Installed Assert packages intentionally do not expose internal hook headers as normal public API. Build hook-enabled tests from the source tree.

## Related pages

- @ref assert_testing
- @ref assert_interactive
