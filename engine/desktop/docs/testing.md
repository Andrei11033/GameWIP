@page desktop_testing Maintainer validation

Desktop combines deterministic source-tree tests with opt-in desktop scenarios.
This page records what each layer proves and which platform state must be
captured when a behavior needs a real Window.

## Automated suite

The `desktop` validation module retains coverage for closed and default behavior, description validation, internal and external fixed event queues,
coalescing and overflow, sticky close intent, ownership and thread affinity, native handles, controls, DPI policy, monitor, mode, color inspection,
renderer occlusion feedback, pointer hit masks, native messages, fullscreen recovery, file drops, redraw, unexpected native destruction, deferred
cleanup, pump reentrancy, custom cursor validation, materialization, DPI reselection, and lifetime, Clipboard validation, native round trips,
transactionality, timeouts, partial publication and cleanup precedence, ChildSurface parent loss, geometry, queues, DPI,
ordering, native hosting, DragDrop IDs/descriptions, effect negotiation, region snapshots, target/lightweight conflicts, target queues, movement
coalescing, terminal-event preservation, parent loss, and injected registration rollback paths.

Process-isolated shutdown coverage uses routed child processes so failures after a suite function returns remain observable. It verifies exact zero
exit codes after a standalone color query, a normal `WM_CLOSE` and final-Window close path, owner-thread exit with retained Window state, and
DragDrop dispatcher exit with both normal and repeatedly failed native revocation.

Renderer-facing publication coverage checks default allocation-free owner-thread getters, enablement validation, allocation failure, idempotent stable
storage, and immediate publication. Enabled high-frequency tests cover compound coherence, DPI, scale, current monitor, presentation, visibility,
interactive state, occlusion, transactional open, close/reopen reuse, and native destruction.

Occlusion coverage distinguishes stable backend capability
(`supports(Capability::OcclusionReporting)`) from current provider state
(`Renderer::hasOcclusionProvider()`).

```powershell
cmake --preset test
cmake --build --preset test
ctest --test-dir build/test -R "validation.tests.desktop|validation.exports.Desktop" --output-on-failure
```

## Public-header isolation

Repository validation compiles each supported Desktop entry header independently:

- `desktop/types.h`
- `desktop/description.h`
- `desktop/events.h`
- `desktop/display.h`
- `desktop/display_info.h`
- `desktop/cursor.h`
- `desktop/child_surface.h`
- `desktop/data_transfer.h`
- `desktop/drag_drop.h`
- `desktop/clipboard.h`
- `desktop/window.h`
- `desktop/renderer_bridge.h`
- `desktop/native/win32.h` on Win32

Installed-consumer validation additionally proves exact-version package discovery, `GameWIP::Desktop` linking, public dependency propagation,
internal-header exclusion, and absence of `DESKTOP_INTERNAL_TEST_HOOKS` in installed consumers.

Clipboard coverage publishes and reads strict UTF-8 including non-BMP text, Unicode nonexistent absolute paths, padded and packed RGBA8, foreign RGB
DIB, arbitrary case-insensitive registered formats, opaque bytes containing zero, and ordered multi-format payloads. Coordinated native contention
validates finite timeout behavior without an acquisition race. Hooks inject preparation, owner, access, clear, read, enumeration, selected
publication, and close failures while checking exact external mutation state. These tests intentionally replace the interactive desktop Clipboard and
clear it when the suite completes.

DragDrop coverage uses the public target API plus source-tree-only effect and
event-injection hooks. It validates closed/default behavior, invalid effects and
IDs, strict custom names, transactional region replacement, internal and
external queue ownership, movement coalescing, terminal Drop priority,
lightweight file-drop conflicts in both directions, native registration
rollback after OLE acquisition and native-target creation, explicit and Window
close retry after revocation failure, whole-chain deferred cleanup with multiple
targets, close/reopen, and normal/unexpected Window loss. Process-isolated
fixtures also cover owner-thread dispatcher exit with an active target, including
repeated revocation failure, followed by destruction of surviving public owners
on another thread. Shared transfer regressions continue through the Clipboard
native round-trip suites.

Interactive source-button release, Escape cancellation, Explorer/application
interop, foreign repeated `IDataObject::GetData` requests, and cross-process
custom schemas require the manual workflow in @ref desktop_manual_validation.

## Suite organization

The exhaustive Desktop validation is one scenario suite, with
responsibility-focused implementation includes for manual scenarios,
lifecycle/state, events, renderer integration, display behavior, Clipboard,
ChildSurface, and DragDrop. Tests use public API except for approved deterministic
source-tree hooks documented by @ref desktop_test_hooks.

## Manual tests

Manual Desktop validation is opt-in from an interactive Windows desktop and
covers native presentation, custom chrome, pointer policies, cursor behavior,
file drops, taskbar/ownership behavior, DPI/fullscreen transitions, and
shell-visible state. See @ref desktop_manual_validation for the operator
checklist.

`--desktop-manual-suite=<name>` accepts `lifecycle`, `multiple-windows`, `custom-chrome`, `layered-pointer`, `dpi`, `cursor`, `child-surface`,
`files-shell`, `drag-drop`, `fullscreen`, `borderless`, `exclusive`, `topology`, `hdr`, and `modern`. `fullscreen` retains the complete workflow; `borderless`,
`exclusive`, and `topology` isolate the display-changing portions for safer reproduction. Manual runs flush every report line and record before/after
mode-transition geometry so evidence survives a driver reset or process interruption.
