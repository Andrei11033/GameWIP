@page window_manual_validation Manual validation scenarios

Run these scenarios in a normal interactive desktop session. Record the OS version, display topology, scale factors, GPU and driver, and pass/fail notes. Before ending a scenario, restore the desktop and Window to their original display mode, cursor-confinement, topmost, and opacity settings.

The opt-in validation runner provides the complete guided workflow:

```powershell
.\build\test\GameWIPTests.exe --test-module=window --manual-tests
```

Answer `yes`, `no`, or `skip` for every manual observation. The runner assigns `GameWIP.Validation.WindowManualTests` as its explicit process AppUserModelID so independent validation Windows appear in a dedicated GameWIP taskbar group instead of being grouped under the launching editor or terminal. Windows may combine multiple independent validation Windows into that one group according to the user's taskbar settings. The diagnostics companion and intentionally owned tool Windows remain excluded from independent taskbar entries.

The always-on-top `GameWIP Window manual-test diagnostics` companion appears near the upper-right of the primary work area and shows the expected outcome and live Window state while the runner keeps native events pumping. Its native section reports the actual HWND frame/client rectangles, monitor rectangle, DPI, style bits, fullscreen conformance, and taskbar eligibility independently from the portable cache. File-drop checks show receipt immediately in that companion and in the validation Window title. A failed setup operation reports portable and native diagnostics and suppresses dependent questions.

The validation executable paints a renderer-free blue/cyan GDI surface while each prompt waits. The inset cyan marker makes the current client boundary visible after resize and fullscreen transitions. Custom-chrome checks draw the installed drag and caption-control regions directly on that surface, then draw the old strip as inactive and the replacement strip in a distinct color after the layout changes. The artwork is owned by the Window validation module because it depends on Window state and Win32 native interop; TestSupport continues to own only the reusable prompt and result-recording contract.

Hardware or topology that is unavailable must be recorded as skipped rather than passed. Renderer-dependent transparency and framebuffer-alpha observations require a renderer-backed host and therefore skip in `GameWIPTests`. Deterministic automated suites own unsafe internal lifecycle, failure-injection, event-ordering, and pointer-mask generation cases identified below.

To repeat one section, add `--window-manual-suite=<name>`. The accepted names are documented in @ref window_testing. Use `borderless`, `exclusive`, or `topology` to isolate the corresponding part of the complete `fullscreen` workflow. Manual report output is flushed per line and includes timestamped before/after cached and native mode geometry for every display-changing request.

## Lifecycle and multiple windows

1. Create a visible focused Window, resize and move it, minimize/maximize/restore it, request close through the system button, decline once with `clearCloseRequest()`, request again, and explicitly close.
2. Open two independent Windows and pump both from one thread without WindowManager. Verify events route to the correct queue and closing one leaves the other operational.
3. Open an owned tool Window, activate and close it, change/remove its owner at runtime, and verify z-order/minimization behavior remains native and stable.
4. Show a hidden Window while another application is focused and verify `show()` does not activate it; then call `requestFocus()` and record the OS-policy result.
5. Where safely reproducible, destroy an open Window object from a non-owner thread, pump the owner dispatcher, and verify that native resources, IDs, icons, cursor state, and exclusive display state are released exactly once.
6. Trigger unexpected native destruction through the approved test scenario. Verify `isOpen()==false`, `lifetimeState()==NativeDestroyedPendingFinalize`, one typed `Types::Events::NativeDestroyed`, `NotOpen` from native mutations, `AlreadyOpen` before finalization, successful owner-thread `close()`, and reopen afterward.
7. Let a Window-owning thread exit while the portable object remains alive elsewhere. Verify the dispatcher restores exclusive state and destroys the HWND before the surviving object is released, with no duplicate cleanup or stale ID.

## Custom chrome

1. Use the labeled teal drag region and visible system, minimize, maximize, and close regions to validate dragging, edge/corner resizing, snap layouts, system menu, minimize, maximize/restore, and close behavior.
2. After the surface changes, verify the red old strip and former controls stop responding while only the green replacement strip drags.
3. Repeat at 100%, 125%, 150%, and 200% scale and after moving between differently scaled monitors.

## Layered and pointer behavior

1. Exercise opacity at 1.0, intermediate values, and 0.0; verify input behavior is unchanged.
2. Create a transparent-framebuffer Window and verify compositor transparency and redraw behavior.
3. Validate whole-window `ClickThrough` against another interactive application below, including client and system-frame areas. Restore `Normal` and verify the system title bar, resize border, and client input work again.
4. Verify `AcceptRegions` and `IgnoreRegions` return `Unsupported` without changing the current pointer mode while `PointerRegions` is false.
5. Test rectangular and per-pixel passthrough only when a future backend advertises the genuine capability. Place a different application underneath; same-thread-only routing is not a pass.
6. Publish first/last-pixel masks, clear them, move the Window, resize the framebuffer, and complete GPU readbacks out of revision order. Verify movement preserves the mask, resize invalidates it, and stale publication cannot win.

## DPI and coordinates

1. Arrange mixed-DPI monitors on both sides of the primary, including negative x or y origins.
2. With `PreserveLogicalClientSize`, cross DPI boundaries and verify logical size remains stable while framebuffer pixels change.
3. With `PreservePhysicalClientSize`, repeat and verify framebuffer pixels remain stable while logical size changes.
4. Validate `clientToScreen()` and `screenToClient()` near every edge and record expected integral-pixel rounding.
5. Verify monitor bounds and work areas remain comparable physical virtual-screen rectangles and are not independently scaled.

## Cursor

1. Exercise every standard cursor shape after the test positions it at the center of the visible client.
2. Validate hidden, confined, hidden-confined, and relative modes while focused.
3. Alt-tab away, minimize, hide, restore, and close while confined/relative; verify the system cursor is always released and exclusive relative centering resumes only while focused.
4. Warp to client corners and validate logical positions at multiple DPI scales.

## Files and shell behavior

1. Enable file drops and drag one file, multiple files, Unicode paths, and paths containing spaces; verify one grouped event and optional client position.
2. Disable drops and confirm no event is produced.
3. Validate the blue/cyan patterned icon at small/large shell sizes, attention flashing, focusability, disabled interaction, topmost toggling, and standard control disabling.
4. Exercise every valid resizable/maximizable combination and both invalid transition orders. Verify closable and minimizable remain independent.
5. Confirm an owned Window has no independent taskbar entry by default; remove and restore the owner and verify styles and taskbar behavior recover.

## Fullscreen and display topology

1. Enter and leave borderless fullscreen on each monitor; verify the blue surface and cyan inset marker reach every display edge, native popup and visible styles and HWND bounds match the monitor, the GameWIP Window remains available through the taskbar or Alt+Tab, and saved windowed placement returns.
2. Enter an enumerated exclusive mode. Alt-tab from the terminal to the validation Window, back, to the validation Window again, and finally back to the terminal to answer. Verify the focused Window covers the display, the test observes both active and suspended states, the inactive state reports `suspended=true`, and desktop mode is restored when leaving and closing.
3. Reject an unsupported exact mode without changing Window or display state.
4. Move between monitors with different DPI and verify logical client geometry, physical framebuffer extent, scale/DPI events, and current monitor.
5. Connect/disconnect or enable/disable a monitor where practical, re-enumerate after the display event, and verify stale monitor IDs fail cleanly.
6. Disconnect the active borderless and exclusive target. Verify exclusive display state is restored and the Window recovers visibly to windowed mode on the surviving primary monitor.
7. Verify recovery clears `FullscreenInfo` and orders events as display configuration, mode, optional monitor, optional geometry/framebuffer, then optional DPI/content scale. Confirm the pump reports any restoration/repositioning failure without leaving stale fullscreen state.

## HDR and advanced color

1. On an SDR-only display, query both the monitor and Window forms. Verify the monitor identity matches, HDR is unsupported and disabled, active color is SDR or unknown only when the driver cannot classify it, and unavailable optional metadata remains zero.
2. On an HDR-capable display with HDR disabled, verify support remains true while `hdrEnabled` is false and the active mode is not reported as HDR merely because channel precision exceeds eight bits.
3. Enable HDR while the Window remains on that monitor. Pump events, verify `Types::Events::DisplayConfigurationChanged` is delivered, re-query, and confirm HDR enablement and `Hdr10Pq` where the driver reports PQ output. Disable HDR and repeat.
4. Move a Window between SDR and HDR monitors. Verify `Types::Events::MonitorChanged`, re-query through the Window form, and confirm the returned monitor and state follow the destination.
5. Where supported, compare minimum, peak, and full-frame luminance against the display/driver report. Verify SDR white level is expressed in nits; at the native value 2500 the public value is 200 nits.
6. Disconnect and reconnect the queried display. Verify the stale `Types::Display::MonitorId` fails safely, enumerate again, and confirm a new query succeeds without stale metadata.
7. When a Windows 10 compatibility environment is available, repeat without the Windows 11 advanced-color query. This is optional compatibility coverage outside the supported Windows 11 development host. Verify the documented legacy query remains functional and unavailable WCG-specific metadata stays unknown rather than fabricated.

## Modern Windows capabilities

1. On Windows 11 build 22621 or newer, apply and clear every `BackdropEffect`; repeat on an older supported build and verify `Unsupported`.
2. On Windows 11 build 26100 or newer, validate `DWMWA_REDIRECTIONBITMAP_ALPHA` output using renderer-provided premultiplied alpha. Repeat on an older build and verify open returns `Unsupported` without a partial Window.
3. Confirm whole-window opacity remains independent from framebuffer alpha.

## Failure observations

For every failed checked operation, record the portable error code, native code, diagnostic text, `isOpen()`, and relevant cached properties. Verify the Window remains either unchanged and retryable or completely closed according to @ref window_lifecycle_events.

## Related pages

- @ref window_coordinates_and_dpi
- @ref window_fullscreen_monitors
- @ref window_testing
