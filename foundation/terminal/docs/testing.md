@page terminal_testing Terminal maintainer validation and hooks

@note This page is for maintainers. Hook declarations are internal, non-installed, and carry no consumer compatibility guarantee.

## Behavior coverage

Terminal tests cover:

- stdout/stderr text, byte, line, formatted, segmented, and buffered writes;
- complete-record emission, per-stream synchronization, and scratch reuse;
- capability queries, output preparation, idempotence, failure propagation, and style fallback;
- `StyleMode::Never`, `Auto`, and `Required`;
- redirected output, UTF-8, native line endings, and invalid-option rejection before emission;
- stdin line/byte/text reads, byte limits, timeouts, would-block, and availability;
- input-mode query/change/restore and preservation of buffered input;
- cursor, alternate-screen, title, bell, clear, size, flush, and failure behavior;
- RAII setup, nesting, explicit restoration, failed restoration, and retry behavior;
- concurrent calls and Logger integration through the shared Terminal runtime.

## Internal hooks

The GameWIP `validation` preset enables hooks automatically. A focused source build can set `TERMINAL_ENABLE_TEST_HOOKS=ON`; approved build-tree targets then receive `INTERNAL_TERMINAL_TEST_HOOKS=1`.

A source-tree test links `Terminal` (or `GameWIP::Terminal`) and includes `terminal/internal/terminal_test_hooks.h`. The build-tree target supplies the source include root and compile definition. Installed packages intentionally do not provide this header.

Hook rules:

- headers remain under `foundation/terminal/internal/` and are not installed;
- names are backend-neutral;
- one-shot failures use `forceNext...`;
- persistent state uses `set...Override` and `clear...Override`;
- a reset helper clears all forced state between scenarios;
- production code must not depend on hook declarations.

Tests cannot claim serialization against `std::cout`, `printf`, direct native writes, or third-party output because those paths do not use Terminal's lock.

GameWIP owns module selection, CTest registration, and reports. See @ref project_testing.
