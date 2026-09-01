@page desktop_manual_validation Manual validation scenarios

Run these scenarios in a normal interactive desktop session. Record the OS version, display topology, scale factors, GPU and driver, and pass/fail
notes. Before ending a scenario, restore the desktop and Window to their original display mode, cursor-confinement, topmost, and opacity settings.

The opt-in validation runner provides the complete guided workflow:

```powershell
.\build\test\GameWIPTests.exe --test-module=desktop --manual-tests
```

Answer `yes`, `no`, or `skip` for every manual observation. The runner assigns `GameWIP.Validation.DesktopManualTests` as its explicit process
AppUserModelID so independent validation Windows appear in a dedicated GameWIP taskbar group instead of being grouped under the launching editor or
terminal. Windows may combine multiple independent validation Windows into that one group according to the user's taskbar settings. The diagnostics
companion and intentionally owned tool Windows remain excluded from independent taskbar entries.

The always-on-top `GameWIP Window manual-test diagnostics` companion appears near the upper-right of the primary work area and shows the expected
outcome and live Window state while the runner keeps native events pumping. Its native section reports the actual HWND frame/client rectangles,
monitor rectangle, DPI, style bits, fullscreen conformance, and taskbar eligibility independently from the portable cache. File-drop checks show
receipt immediately in that companion and in the validation Window title. A failed setup operation reports portable and native diagnostics and
suppresses dependent questions.

The validation executable paints a renderer-free blue/cyan GDI surface while each prompt waits. The inset cyan marker makes the current client
boundary visible after resize and fullscreen transitions. Custom-chrome checks draw the installed drag and caption-control regions directly on that
surface, then draw the old strip as inactive and the replacement strip in a distinct color after the layout changes. The artwork is owned by the
Desktop validation module because it depends on Window state and Win32 native interop; TestSupport continues to own only the reusable prompt and
result-recording contract.

Hardware or topology that is unavailable must be recorded as skipped rather than passed. Renderer-dependent transparency and framebuffer-alpha
observations require a renderer-backed host and therefore skip in `GameWIPTests`. Deterministic automated suites own unsafe internal lifecycle,
failure-injection, event-ordering, and pointer-mask generation cases identified below.

To repeat one section, add `--desktop-manual-suite=<name>`. The accepted names are documented in @ref desktop_testing. Use `borderless`, `exclusive`,
or `topology` to isolate the corresponding part of the complete `fullscreen` workflow. Manual report output is flushed per line and includes
timestamped before/after cached and native mode geometry for every display-changing request.

## Lifecycle and multiple windows

1. Create a visible focused Window, resize and move it, minimize, maximize, and restore it, request close through the system button, decline once with
   `clearCloseRequest()`, request again, and explicitly close.
2. Open two independent Windows and pump both from one thread without WindowManager. Verify events route to the correct queue and closing one leaves
   the other operational.
3. Open an owned tool Window, activate and close it, change or remove its owner at runtime, and verify z-order and minimization behavior remain
   native and stable.
4. Show a hidden Window while another application is focused and verify `show()` does not activate it; then call `requestFocus()` and record the
   OS-policy result.
5. Where safely reproducible, destroy an open Window object from a non-owner thread, pump the owner dispatcher, and verify that native resources, IDs,
   icons, cursor state, and exclusive display state are released exactly once.
6. Trigger unexpected native destruction through the approved test scenario. Verify `isOpen()` returns false,
   `lifetimeState()==NativeDestroyedPendingFinalize`, one typed `Types::Events::NativeDestroyed`, `NotOpen` from native mutations, `AlreadyOpen`
   before finalization, successful owner-thread `close()`, and reopen afterward.
7. Let a Window-owning thread exit while the portable object remains alive elsewhere. Verify the dispatcher restores exclusive state and destroys the
   HWND before the surviving object is released, with no duplicate cleanup or stale ID.

## Custom chrome

1. Use the labeled teal drag region and visible system, minimize, maximize, and close regions to validate dragging, edge/corner resizing, snap
   layouts, system menu, minimize, maximize/restore, and close behavior.
2. After the surface changes, verify the red old strip and former controls stop responding while only the green replacement strip drags.
3. Repeat at 100%, 125%, 150%, and 200% scale and after moving between differently scaled monitors.

## Native child surfaces

1. Run `--desktop-manual-suite=child-surface` and verify the labeled Win32 button appears as a real descendant inside the ChildSurface region.
2. Move the parent across mixed-DPI monitors and verify the host preserves its logical rectangle while its physical extent follows the destination DPI.
3. Confirm the external descendant is destroyed before `ChildSurface::close()`, then the parent closes without stale native UI or taskbar state.

## Layered and pointer behavior

1. Exercise opacity at 1.0, intermediate values, and 0.0; verify input behavior is unchanged.
2. Create a transparent-framebuffer Window and verify compositor transparency and redraw behavior.
3. Validate whole-window `ClickThrough` against another interactive application below, including client and system-frame areas. Restore `Normal` and
   verify the system title bar, resize border, and client input work again.
4. Verify `AcceptRegions` and `IgnoreRegions` return `Unsupported` without changing the current pointer mode while `PointerRegions` is false.
5. Test rectangular and per-pixel pass-through only when a future backend advertises the genuine capability. Place a different application underneath;
   same-thread-only routing is not a pass.
6. Publish first/last-pixel masks, clear them, move the Window, resize the framebuffer, and complete GPU readbacks out of revision order. Verify
   movement preserves the mask, resize invalidates it, and stale publication cannot win.

## DPI and coordinates

1. Arrange mixed-DPI monitors on both sides of the primary, including negative x or y origins.
2. With `PreserveLogicalClientSize`, cross DPI boundaries and verify logical size remains stable while framebuffer pixels change.
3. With `PreservePhysicalClientSize`, repeat and verify framebuffer pixels remain stable while logical size changes.
4. Validate `clientToScreen()` and `screenToClient()` near every edge and record expected integral-pixel rounding.
5. Verify monitor bounds and work areas remain comparable physical virtual-screen rectangles and are not independently scaled.

## Cursor

1. Exercise every standard cursor shape after the test positions it at the center of the visible client.
2. Validate hidden, confined, hidden-confined, and relative modes while focused.
3. Alt-tab away, minimize, hide, restore, and close while confined/relative; verify the system cursor is always released and exclusive relative
   centering resumes only while focused.
4. Warp to client corners and validate logical positions at multiple DPI scales.
5. Select one custom image, switch through hidden and relative modes, and verify normal mode restores the same custom cursor.
6. Move a multi-variant custom cursor across mixed-DPI monitors and verify the intended physical size and hotspot follow each destination without a
   visible resource rebuild.
7. Share one custom cursor across two Windows, restore a system shape on one Window, and verify the other Window keeps its custom selection.

## Files and shell behavior

1. Enable file drops and drag one file, multiple files, Unicode paths, and paths containing spaces; verify one grouped event and optional client
   position.
2. Disable drops and confirm no event is produced.
3. Validate the blue/cyan patterned icon at small/large shell sizes, attention flashing, focusability, disabled interaction, topmost toggling, and
   standard control disabling.
4. Exercise every valid resizable/maximizable combination and both invalid transition orders. Verify closable and minimizable remain independent.
5. Confirm an owned Window has no independent taskbar entry by default; remove and restore the owner and verify styles and taskbar behavior recover.

## Clipboard interoperability

These checks use normal desktop applications and do not require an open GameWIP Window:

1. Publish ASCII, multibyte UTF-8, and non-BMP text from GameWIP; paste into Notepad and verify exact visible text. Copy text from Notepad and verify
   `readText()` returns the expected UTF-8.
2. Publish several absolute Unicode/nonexistent paths and inspect them with a compatible Explorer/desktop drop workflow. Copy real files in Explorer
   and verify `readFiles()` preserves native order and spelling without reading file contents.
3. Publish a small RGBA image with transparent and opaque pixels; paste into a common image-capable application and verify orientation, channel order,
   and alpha. Copy an RGB image from that application and verify GameWIP returns alpha 255 where native alpha is not explicit.
4. Run two independent processes that agree on one registered format name and schema. Publish binary bytes including `0x00` in one process and verify
   the other reads the opaque block. Repeat with names differing only by case and confirm they identify the same Win32 format.
5. Hold the Clipboard open in an external diagnostic process. Verify `kNoWait` returns promptly and a finite explicit timeout remains bounded without
   busy spinning. Release it and verify the next operation succeeds.
6. Record that immediate zero-byte custom publication reports `Unsupported` without clearing existing contents; do not substitute a one-byte payload
   or delayed renderer.

Record the applications/versions used and whether each direction passed. Custom interoperability proves only the agreed name/schema, not universal
interpretation of arbitrary registered formats.

## Native data drag and drop

Record the source/target applications and exact formats for each direction.
Run the dedicated guided source/target harness from the repository root:

```powershell
.\build\test\GameWIPTests.exe --test-module=desktop --manual-tests --desktop-manual-suite=drag-drop
```

The green source Window starts `beginDrag()` when the requested mouse button is
held inside it. The blue target uses a whole-client Region 1 with Copy preferred;
its inset green Region 2 overlaps it and prefers Move. Live `Entered`, `Moved`,
region-transition, `Left`, and `Dropped` counts appear in the diagnostics Window.
The runner checks same-process payload bytes and negotiated effects after each
accepted prompt. Answer `skip`—never `yes`—when a controlled custom or malformed
`IDataObject` provider/consumer is unavailable.

1. Open one full target with overlapping whole-client and rectangular regions.
   Resize the Window and cross region boundaries. Verify the last matching
   supplied region wins, the whole-client region follows resize, one Entered and
   one Left delimit the top-level session, and region changes appear only as
   Moved previous/current IDs.
2. Drag UTF-8 text, one and several files (including Unicode paths), an image
   with visible alpha/orientation markers, and an agreed custom binary format
   from foreign applications into GameWIP. Verify the final event owns the
   complete payload in accepted-region order.
3. Begin GameWIP source drags for each portable format and consume them in
   Explorer or another compatible application. Request the same native format
   repeatedly with a diagnostic consumer and verify every request succeeds after
   caller source storage has gone out of scope.
4. Advertise several source effects. Verify target preference and Copy/Move/Link
   fallback, then advertise one source effect to force it. Hold Ctrl, Shift, and
   Alt during movement and verify they do not alter portable negotiation.
5. Start with Left, Right, and Middle trigger buttons in separate runs. Verify a
   configured button that is not held rejects before modal entry, releasing the
   configured button requests completion, Escape reports successful
   cancellation, and unrelated-button changes do not terminate the drag.
6. Drag between a GameWIP source Window and a second GameWIP target in the same
   process. Verify normal Window geometry/presentation events continue updating
   during the modal loop and the target queues a complete Drop.
7. Use a diagnostic foreign provider that advertises multiple formats, fails a
   later selected request, returns malformed Unicode/DIB/HDROP, and exposes an
   excessive enumeration. Verify every malformed or partial case returns
   Effect::None and queues no successful Dropped event.
8. Enable lightweight file drops and verify full target open reports
   ResourceBusy without disabling them. Disable lightweight mode, open the full
   target, and verify re-enabling lightweight mode likewise reports ResourceBusy.
9. Cancel and complete source operations using Copy, Move, and Link. Confirm Move
   never deletes, renames, or mutates the source data.
Win32 immediate publication cannot represent an exact zero-byte custom
`HGLOBAL`; record `Unsupported` without substituting a byte or delayed provider.

## Fullscreen and display topology

1. Enter and leave borderless fullscreen on each monitor; verify the blue surface and cyan inset marker reach every display edge, native popup and
   visible styles and HWND bounds match the monitor, the GameWIP Window remains available through the taskbar or Alt+Tab, and saved windowed placement
   returns.
2. Enter an enumerated exclusive mode. Use Alt+Tab to move from the terminal to the validation Window, back, to the validation Window again, and
   finally back to the terminal to answer. Verify the focused Window covers the display, the test observes both active and suspended states, the
   inactive state reports `suspended=true`, and desktop mode is restored when leaving and closing.
3. Reject an unsupported exact mode without changing Window or display state.
4. Move between monitors with different DPI and verify logical client geometry, physical framebuffer extent, scale/DPI events, and current monitor.
5. Connect/disconnect or enable/disable a monitor where practical, re-enumerate after the display event, and verify stale monitor IDs fail cleanly.
6. Disconnect the active borderless and exclusive target. Verify exclusive display state is restored and the Window recovers visibly to windowed mode
   on the surviving primary monitor.
7. Verify recovery clears `FullscreenInfo` and orders events as display configuration, mode, optional monitor, optional geometry/framebuffer, then
   optional DPI/content scale. Confirm the pump reports any restoration/repositioning failure without leaving stale fullscreen state.

## HDR and advanced color

1. On an SDR-only display, query both the monitor and Window forms. Verify the monitor identity matches, HDR is unsupported and disabled, active color
   is SDR or unknown only when the driver cannot classify it, and unavailable optional metadata remains zero.
2. On an HDR-capable display with HDR disabled, verify support remains true while `hdrEnabled` is false and the active mode is not reported as HDR
   merely because channel precision exceeds eight bits.
3. Enable HDR while the Window remains on that monitor. Pump events, verify `Types::Events::DisplayConfigurationChanged` is delivered, re-query, and
   confirm HDR enablement and `Hdr10Pq` where the driver reports PQ output. Disable HDR and repeat.
4. Move a Window between SDR and HDR monitors. Verify `Types::Events::MonitorChanged`, re-query through the Window form, and confirm the returned
   monitor and state follow the destination.
5. Where supported, compare minimum, peak, and full-frame luminance against the display/driver report. Verify SDR white level is expressed in nits; at
   the native value 2500 the public value is 200 nits.
6. Disconnect and reconnect the queried display. Verify the stale `Types::Display::MonitorId` fails safely, enumerate again, and confirm a new query
   succeeds without stale metadata.
7. When a Windows 10 compatibility environment is available, repeat without the Windows 11 advanced-color query. This is optional compatibility
   coverage outside the supported Windows 11 development host. Verify the documented legacy query remains functional and unavailable WCG-specific
   metadata stays unknown rather than fabricated.

## Modern Windows capabilities

1. On Windows 11 build 22621 or newer, apply and clear every `BackdropEffect`; repeat on an older supported build and verify `Unsupported`.
2. On Windows 11 build 26100 or newer, validate `DWMWA_REDIRECTIONBITMAP_ALPHA` output using renderer-provided premultiplied alpha. Repeat on an older
   build and verify open returns `Unsupported` without a partial Window.
3. Confirm whole-window opacity remains independent from framebuffer alpha.

## Failure observations

For every failed checked operation, record the portable error code, native code, diagnostic text, `isOpen()`, and relevant cached properties. Verify
the Window remains either unchanged and retryable or completely closed according to @ref desktop_lifecycle_events.

## Related pages

- @ref desktop_coordinates_and_dpi
- @ref desktop_fullscreen_monitors
- @ref desktop_testing
- @ref desktop_clipboard
