@page window_testing Maintainer validation

## Automated suite

The `window` validation module covers:

- Passive values, closed-state defaults, non-copyable and non-movable ownership, and invalid descriptions and enum values.
- Internal and external event queues, coalescing, overflow, sticky close intent, and hidden native lifecycle behavior.
- Owned windows, taskbar styles, native handles, controls, DPI policy calculations, and runtime capability snapshots.
- Renderer occlusion feedback, monitor discovery, DisplayConfig preferred modes, display-color status and classification, refresh-rate conversion, exact native-mode comparison, and packed-mask publication.
- Native messages for size, focus, cursor presence, display changes and recovery ordering, color changes, file drops, redraw, close requests, and unexpected destruction.
- Win32 frame mutations preserve live visibility, disabled, minimized, maximized, and file-drop styles instead of replacing runtime-managed native state.
- Wrong-thread mutation, close, and native-handle rejection; deferred destruction; owner-thread dispatcher exit; renderer feedback and display-color queries; cross-thread wake; and pump reentrancy.

One-shot hooks cover allocation, dispatcher setup, native creation, partial-open rollback, mutations, native queries, fullscreen rollback and restoration, close, event pumping, and retry after recoverable failures.

State hooks validate pending finalization, forced fullscreen-target removal, display-color conversion and change notification, packed-mask storage and revisions, DPI-transition calculations, refresh-rate rounding, and exact exclusive-mode matching without changing desktop configuration.

```powershell
cmake --preset test
cmake --build --preset test
ctest --test-dir build/test -R "validation.tests.window|validation.exports.Window" --output-on-failure
```

The repository installed-consumer test additionally proves exact-version dependency discovery, `GameWIP::Window` linking, installed portable/native/integration headers, internal-header exclusion, and absence of installed hook definitions.

## Manual tests

Automated runs keep manual interaction disabled. From a normal interactive Windows desktop session, run the complete interactive suite:

```powershell
.\build\test\GameWIPTests.exe --test-module=window --manual-tests
```

The command runs grouped suites for:

- Visible lifecycle, multiple Windows, ownership changes, activation, and close behavior.
- Custom chrome, opacity, compositor transparency, and cross-application pointer routing.
- Mixed-DPI policies, coordinate conversion, cursor shapes and modes, confinement, and warping.
- File drops, native icons, attention, interaction controls, topmost behavior, and taskbar ownership.
- Borderless and exclusive fullscreen, monitor movement, topology changes, and target-disconnect recovery.
- SDR/HDR facts and transitions, display reconnect, system backdrops, and framebuffer alpha.

The harness opens an always-on-top, read-only diagnostics companion near the upper-right of the primary work area. Its title is `GameWIP Window manual-test diagnostics`. It displays the current scenario, expected result, latest event-specific observation, cached geometry, DPI, monitor, mode, fullscreen suspension, presentation, opacity, cursor, pointer, controls, ownership, file-drop, topmost, focus, and interaction state. A separate native section queries the HWND directly and reports its frame, client rectangle, nearest monitor rectangle, DPI, style bits, popup-style result, fullscreen-bounds result, and taskbar eligibility. This distinction makes a disagreement between the portable cache and the actual native Window explicit. The owner thread refreshes this display and continues pumping Window events while the console waits for each response. File-drop scenarios also change the validation Window title as soon as a grouped event arrives.

`GameWIPTests` has no renderer, so the Window module paints a test-only Win32 GDI surface after every event-pump interval. The standard surface uses a blue client fill, a cyan inset edge marker, and centered labels. Fullscreen validation therefore exposes the complete current client area instead of retaining undefined pixels from the old windowed backing. Custom-chrome scenarios replace the generic artwork with labeled drag, system-menu, minimize, maximize, close, inactive, and replacement regions that match the logical rectangles installed by the test. Multiple-window scenarios repaint both the owner and owned Window. The surface is validation artwork only and is not a Window rendering API or a renderer-integration example.

Cursor-shape checks move the pointer to the center of the validation client before each prompt. Corner-warp checks compare the queried logical position with the requested position rather than checking query success alone. The shell-icon check uses a recognizable blue/cyan RGBA pattern at both required sizes.

The manual Window module assigns the process the explicit AppUserModelID `GameWIP.Validation.WindowManualTests` before it creates any native Window. This prevents the validation Windows from being grouped under VS Code, Windows Terminal, or another launching host. Independent validation Windows share a dedicated GameWIP taskbar group; Windows can combine multiple members into one button according to the user's taskbar settings, and each remains available through its group thumbnails and Alt+Tab.

The diagnostics companion is a tool Window and intentionally has no taskbar entry. Owned tool Windows also intentionally omit an independent entry because that is the ownership behavior under test. Every other separately titled validation Window must report `taskbarEligible=true`, including borderless and exclusive fullscreen. Borderless and active exclusive modes must also report `popupStyle=true`, `visibleStyle=true`, and `fullscreenBounds=true`; those conditions are asserted before the manual prompt. Expose the taskbar or use Alt+Tab to verify that the shell actually presents the dedicated GameWIP group, because native eligibility is necessary but does not replace the shell observation.

The exclusive activation prompt records both focused/active and unfocused/suspended states. Alt-tab from the terminal to the validation Window, back to the terminal, to the validation Window again, and finally back to the terminal to answer. While the target is focused it must cover the display. After returning to the terminal, `suspended=true` and a windowed-sized validation surface can be expected because the backend has restored the desktop display mode for the inactive exclusive Window.

Public-API setup and state assertions run before the related manual observation. A failed prerequisite reports its portable code, native code, and diagnostic text, then skips dependent observations instead of asking about a state that was never entered. Capability-dependent scenarios skip automatically when the backend does not advertise support. Renderer-dependent alpha observations skip in `GameWIPTests` because this executable does not attach an alpha-producing renderer. Hardware-, renderer-, operating-system-, and topology-dependent prompts accept an explicit skip; a skip must not be reported as passed evidence.

Run one group while investigating or repeating a failure:

```powershell
.\build\test\GameWIPTests.exe --test-module=window --manual-tests --window-manual-suite=fullscreen
```

Accepted selectors are `lifecycle`, `multiple-windows`, `custom-chrome`, `layered-pointer`, `dpi`, `cursor`, `files-shell`, `fullscreen`, `hdr`, and `modern`. An unknown selector fails validation. The selector narrows only the Window module; `--manual-tests` remains the shared opt-in flag.

The scenario definitions and required environment record remain authoritative in @ref window_manual_validation. Deterministic suites cover queue behavior, pointer-mask generations, exceptional destruction, cross-thread destruction, thread-exit cleanup, fullscreen recovery ordering, and injected failures without requiring unsafe manual reproduction.

## Public-header checks

`window/window.h` compiles first in isolation. `window/native/win32.h` has a Win32-only check. `window/renderer_bridge.h` compiles independently and preserves the portable boundary.

## Export boundary

`cmake/export_allowlists/window.txt` records normalized public symbol roots. Source-tree hooks are permitted only when `WINDOW_ENABLE_TEST_HOOKS` is enabled and are ignored by the public export comparison. Internal backend declarations are absent from the import library.

## Scope

Automated tests keep native windows hidden. The scenarios in @ref window_manual_validation cover behavior that requires an interactive desktop or physical hardware, including:

- Visible window and shell behavior.
- Cross-application pointer routing and operating-system focus arbitration.
- Drag-and-drop gestures and transparent renderer output.
- DPI transitions across physical monitors.
- Monitor disconnection and reconnection.
- SDR and HDR transitions.
- Exclusive display-mode restoration.

Before submission, the change owner runs the final repository validation matrix and records the environment and results in the pull request. During implementation, focused builds and Window/package checks are sufficient to catch local regressions without claiming complete platform validation.

## Related pages

- @ref window_test_hooks
- @ref window_manual_validation
- @ref window_package_abi
