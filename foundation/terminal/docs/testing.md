@page terminal_testing Terminal testing

Terminal-specific tests and internal test hooks are implemented. Hook-dependent tests run when `TERMINAL_ENABLE_TEST_HOOKS=ON` configures `INTERNAL_TERMINAL_TEST_HOOKS=1`; hook-disabled builds skip those deterministic backend-hook suites.

Run the project test executable through CTest:

```text
ctest --test-dir build-optimized-debuggable --output-on-failure
```

## Normal behavior coverage

Terminal implementation tests should cover:

- stdout text write;
- stderr text write;
- byte write;
- line write behavior through `writeLine()` and `LineWriteOptions`;
- formatted write behavior through `print()` and `println()`;
- observational capability queries and explicit or lazy output preparation;
- preparation idempotence, failure propagation, and `StyleMode::Auto` fallback;
- one backend text write for styled lines, formatted lines, and text segment batches;
- full-batch rejection before output for invalid or unsupported segments;
- independent stdout and stderr synchronization and scratch storage;
- buffered write behavior through `OutputBuffer`;
- styled output fallback;
- `StyleMode::Never`;
- `StyleMode::Auto`;
- `StyleMode::Required`;
- segmented writes;
- terminal redirection behavior;
- stdin line read;
- byte read;
- text read max byte limits;
- timeout and would-block outcomes;
- input availability;
- input mode get, set, and restore;
- RAII input mode restoration, including complete native-mode snapshots where the backend has additional flags;
- preservation of Terminal-buffered input across mode changes;
- invalid read and write options rejected before input consumption or output emission;
- flush success and failure behavior for each backend stream kind;
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
TERMINAL_ENABLE_TEST_HOOKS
```

This maps to the library-local `TERMINAL_ENABLE_TEST_HOOKS` option and exports:

```text
INTERNAL_TERMINAL_TEST_HOOKS
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
