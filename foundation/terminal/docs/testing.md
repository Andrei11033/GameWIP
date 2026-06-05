@page foundation_terminal_testing Terminal testing

This page documents the Terminal validation strategy.

Terminal-specific tests and internal test hooks are implemented. Hook-dependent tests run when `GAMEWIP_ENABLE_TERMINAL_TEST_HOOKS=ON` configures `GAMEWIP_TERMINAL_TEST_HOOKS=1`; hook-disabled builds skip those deterministic backend-hook suites.

## Normal behavior coverage

Terminal implementation tests should cover:

- stdout text write;
- stderr text write;
- byte write;
- line write behavior through `writeLine()` and `LineWriteOptions`;
- formatted write behavior through `print()` and `println()`;
- buffered write behavior through `OutputBuffer`;
- styled output fallback;
- `StyleMode::Never`;
- `StyleMode::Auto`;
- `StyleMode::Always`;
- segmented writes;
- terminal redirection behavior;
- stdin line read;
- byte read;
- text read max byte limits;
- timeout and would-block outcomes;
- input availability;
- input mode get, set, and restore;
- RAII input mode restoration;
- terminal size query;
- clear;
- cursor movement;
- cursor position query;
- alternate screen enter and leave;
- cursor visibility;
- title;
- bell;
- Unicode text;
- failure hooks.

## Test hooks

Terminal test hooks are controlled by:

```text
GAMEWIP_ENABLE_TERMINAL_TEST_HOOKS
```

This maps to the library-local `TERMINAL_TEST_HOOKS` option and exports:

```text
GAMEWIP_TERMINAL_TEST_HOOKS
```

Hooks are internal only. They are not production API and are not installed as public headers.

Terminal hooks should:

- live under `foundation/terminal/internal/`;
- use backend-neutral names;
- be resettable;
- use `forceNext...` for one-shot failures;
- use `set...Override` and `clear...Override` for persistent overrides;
- provide a `resetAll` style helper when more than one hook exists.

Hook-enabled tests should reset forced state after each scenario.

## Threading and performance validation

Terminal public calls should serialize per stream.

Tests should not assume Terminal can prevent interleaving with `std::cout`, `std::cerr`, `printf`, direct OS writes, or third-party terminal writes.

Successful hot paths should avoid unnecessary allocation. `StyleMode::Never` should avoid style overhead, and segmented writes should be the preferred logger/tool output path.
