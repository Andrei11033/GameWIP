@page window_testing Maintainer validation

## Automated suite

The `window` validation module covers passive values, closed-state defaults, compile-time ownership rules, invalid descriptions and enum values, internal and external queue contracts, coalescing barriers, overflow and drop counters, sticky close intent, move construction, hidden native lifecycle, owned windows, native handle access, renderer occlusion feedback, capabilities, monitors, and display modes. Hidden native-message tests cover size, focus, cursor-presence, display-change, file-drop, redraw, and close-request translation. Threading checks cover wrong-thread mutation and close rejection, wrong-thread renderer feedback, cross-thread wake, and pump reentrancy.

One-shot hooks cover allocation, dispatcher, creation, partial-open rollback, title/region/icon/cursor failures, monitor/display query failures, fullscreen rollback and restoration failure, close failure, event-pump failure, and successful retry after recoverable failures.

```powershell
cmake --preset test
cmake --build --preset test
ctest --test-dir build/test -R "validation.tests.window|validation.exports.Window" --output-on-failure
```

The repository installed-consumer test additionally proves exact-version dependency discovery, `GameWIP::Window` linking, installed portable/native/integration headers, internal-header exclusion, and absence of installed hook definitions.

## Public-header checks

`window/window.h` compiles as the first include in an isolated translation unit. `window/native/win32.h` has a separate check because including native platform types is deliberate only at that boundary. `window/integration/renderer_feedback.h` also compiles independently and preserves the portable boundary.

## Export boundary

`cmake/export_allowlists/window.txt` records normalized public symbol roots. Source-tree hooks are permitted only when `WINDOW_ENABLE_TEST_HOOKS` is enabled and are ignored by the public export comparison. Internal backend declarations are absent from the import library.

## Scope

Automated tests keep native windows hidden. Visible behavior, real focus arbitration, shell visuals, drag/drop gestures, DPI transitions across physical monitors, and exclusive display restoration need the scenarios in @ref window_manual_validation.

The user performs the final repository validation matrix. During implementation, focused builds and Window/package checks are sufficient to catch local regressions without claiming complete platform validation.
