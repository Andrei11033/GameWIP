@page gamewip_platform_backends Platform backend contract

GameWIP libraries keep public APIs platform-neutral. Platform-specific behavior lives behind small backend contracts selected by `GAMEWIP_PLATFORM_ID`.

## Build Selection

`GAMEWIP_PLATFORM_ID` is the build-system platform key. Current Windows builds resolve it to `win32`.

Each library can provide backend implementation files under:

```text
tools/<library>/platform/<id>/*.cpp
```

Backend source files follow this naming convention:

```text
platform/<id>/<id>_<feature>.cpp
```

For example:

```text
tools/logger/platform/win32/win32_logger.cpp
tools/test_support/platform/win32/win32_environment.cpp
```

Each backend folder may also provide:

```text
platform/<id>/platform.cmake
```

Use `platform.cmake` only for that backend's target-local needs, such as platform libraries, compile definitions, resources, or manifests. Adding a new platform should normally mean adding the matching `platform/<id>/` folder with its `.cpp` files and optional `platform.cmake`.

## Logger Backend

Logger core code must not call operating-system APIs directly. A Logger backend implements the functions declared in `tools/logger/internal/logger_platform.h`.

| Area | Backend responsibility |
| --- | --- |
| Debug output | Write text to the platform debugger or diagnostic stream. |
| Fatal popup | Show a fatal diagnostic popup when enabled by Logger config. |
| File IO | Create directories, open exclusive log files, write, flush, close, and report whether a file handle is open. |
| Time | Format local time text used by log file names and log lines. |
| Console color | Report whether stdout or stderr supports ANSI color. |
| Memory | Return best-effort process memory diagnostics. |
| Scratch storage | Provide per-thread formatting scratch storage for header-only formatted logging paths. |

Filtered `LOGGER_*` macro calls check `shouldLog()` before evaluating message or format arguments. Direct formatted Logger calls cannot prevent normal C++ argument evaluation before the call; use macros or an explicit `shouldLog()` guard for expensive arguments.

## Assert Backend

Assert core code must not call operating-system APIs directly. An Assert backend implements the functions declared in `tools/debug/assert/internal/assert_platform.h`.

| Area | Backend responsibility |
| --- | --- |
| Popup | Show a non-interactive assertion/error popup when configured. |
| Action dialog | Show an interactive assertion dialog and return the selected action. |
| Debugger check | Report whether a debugger is currently attached. |
| Debug break | Trigger the platform debug-break instruction or trap. |

Assert passing paths should stay allocation-free and branch-light. Macro documentation must stay clear about which condition and message expressions are evaluated in enabled and disabled builds.

## TestSupport Backend

TestSupport core code must not call operating-system APIs directly. A TestSupport backend implements the functions declared in `tools/test_support/internal/test_support_platform.h` and platform-specific child-process helpers used by the public TestSupport API.

| Area | Backend responsibility |
| --- | --- |
| Environment variables | Read, set, and unset process environment variables. |
| Child process | Run a child process, collect exit code/stdout/stderr, and report launch/read failures. |

TestSupport stays organized as one public include. Its manual pages group features by reporting, expectations, filesystem/environment helpers, child processes, timing, and stress utilities.
