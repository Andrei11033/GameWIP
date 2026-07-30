@page window_testing Maintainer validation

## Automated suite

The `window` validation module covers passive values, closed-state defaults, compile-time non-copyable/non-movable ownership, invalid descriptions and enums, internal and external queue contracts, coalescing, overflow, sticky close intent, hidden native lifecycle, owned windows and taskbar styles, native handles, controls, DPI policy calculations, renderer occlusion feedback, runtime capability snapshots, monitors, DisplayConfig preferred modes, display-color status and classification, rational refresh conversion, exact native mode comparison, and packed-mask publication. Native-message tests cover size, focus, cursor presence, display changes and recovery order, color-change notification, file drops, redraw, close requests, and unexpected destruction. Threading checks cover wrong-thread mutation/close/native-handle rejection, deferred destruction, owner-thread dispatcher exit, renderer feedback and display-color queries, cross-thread wake, and pump reentrancy.

One-shot hooks cover allocation, dispatcher, creation, partial-open rollback, title/region/icon/cursor failures, monitor/display/color query failures, fullscreen rollback and restoration failure, close failure, event-pump failure, and successful retry after recoverable failures. State hooks validate pending finalization, forced fullscreen-target removal, display-color conversion and change notification, packed-mask contents/storage/revisions, DPI transition calculations, rational refresh rounding, and exact exclusive-mode matching without requiring destructive desktop changes.

```powershell
cmake --preset test
cmake --build --preset test
ctest --test-dir build/test -R "validation.tests.window|validation.exports.Window" --output-on-failure
```

The repository installed-consumer test additionally proves exact-version dependency discovery, `GameWIP::Window` linking, installed portable/native/integration headers, internal-header exclusion, and absence of installed hook definitions.

## Public-header checks

`window/window.h` compiles first in isolation. `window/native/win32.h` has a Win32-only check. `window/renderer.h` compiles independently and preserves the portable boundary.

## Export boundary

`cmake/export_allowlists/window.txt` records normalized public symbol roots. Source-tree hooks are permitted only when `WINDOW_ENABLE_TEST_HOOKS` is enabled and are ignored by the public export comparison. Internal backend declarations are absent from the import library.

## Related pages

- @ref window_test_hooks
- @ref window_manual_validation
- @ref window_package_abi

## Scope

Automated tests keep native windows hidden. Visible behavior, genuine cross-application pointer routing, real focus arbitration, shell visuals, drag/drop gestures, DPI transitions across physical monitors, actual monitor disconnect/reconnect, physical SDR/HDR transitions, transparent renderer output, and exclusive display restoration need the scenarios in @ref window_manual_validation.

The user performs the final repository validation matrix. During implementation, focused builds and Window/package checks are sufficient to catch local regressions without claiming complete platform validation.
