@page window_testing Maintainer validation

## Automated suite

The `window` validation module retains coverage for closed/default behavior, description validation, internal/external fixed event queues, coalescing/overflow, sticky close intent, ownership/thread affinity, native handles, controls, DPI policy, monitor/mode/color inspection, renderer occlusion feedback, pointer hit masks, native messages, fullscreen recovery, file drops, redraw, unexpected native destruction, deferred cleanup, pump reentrancy, and injected failure/rollback paths.

The public API pass changes names/ownership without reducing those behavior scenarios. The occlusion test now distinguishes stable backend capability (`supports(Capability::OcclusionReporting)`) from current provider state (`Renderer::hasOcclusionProvider()`).

```powershell
cmake --preset test
cmake --build --preset test
ctest --test-dir build/test -R "validation.tests.window|validation.exports.Window" --output-on-failure
```

## Public-header isolation

Repository validation compiles each supported Window entry header independently:

- `window/types.h`
- `window/description.h`
- `window/events.h`
- `window/display.h`
- `window/display_info.h`
- `window/window.h`
- `window/renderer_bridge.h`
- `window/native/win32.h` on Win32

Installed-consumer validation additionally proves exact-version package discovery, `GameWIP::Window` linking, public dependency propagation, internal-header exclusion, and absence of `WINDOW_INTERNAL_TEST_HOOKS` in installed consumers.

## Source-tree validation migration

The exhaustive Window validation remains one scenario suite, with responsibility-focused implementation includes for manual scenarios, lifecycle/state, events, renderer integration, and display behavior. Tests use the standardized public API directly; no migration adapter or compatibility vocabulary is involved.

## Manual tests

Manual Window validation remains opt-in from an interactive Windows desktop and continues to cover native presentation, custom chrome, pointer policies, cursor behavior, file drops, taskbar/ownership behavior, DPI/fullscreen transitions, and shell-visible state. See `manual_validation.md` for the operator checklist.

`--window-manual-suite=<name>` accepts `lifecycle`, `multiple-windows`, `custom-chrome`, `layered-pointer`, `dpi`, `cursor`, `files-shell`, `fullscreen`, `borderless`, `exclusive`, `topology`, `hdr`, and `modern`. `fullscreen` retains the complete workflow; `borderless`, `exclusive`, and `topology` isolate the display-changing portions for safer reproduction. Manual runs flush every report line and record before/after mode-transition geometry so evidence survives a driver reset or process interruption.
