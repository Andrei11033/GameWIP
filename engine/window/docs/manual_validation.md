@page window_manual_validation Manual validation scenarios

Run these scenarios in a normal interactive desktop session. Record OS version, display topology, scale factors, GPU/driver, and pass/fail notes. Always return exclusive modes, cursor confinement, topmost policy, and opacity to normal before ending a scenario.

## Lifecycle and multiple windows

1. Create a visible focused Window, resize and move it, minimize/maximize/restore it, request close through the system button, decline once with `clearCloseRequest()`, request again, and explicitly close.
2. Open two independent Windows and pump both from one thread without WindowManager. Verify events route to the correct queue and closing one leaves the other operational.
3. Open an owned tool Window, activate and close it, change/remove its owner at runtime, and verify z-order/minimization behavior remains native and stable.
4. Show a hidden Window while another application is focused and verify `show()` does not activate it; then call `requestFocus()` and record the OS-policy result.
5. Where safely reproducible, destroy an open Window from a non-owner thread, pump the owner dispatcher, and verify native resources, IDs, icons, cursor state, and exclusive display state are released once.
6. Trigger unexpected native destruction through the approved test scenario. Verify `isOpen()==false`, `lifetimeState()==NativeDestroyedPendingFinalize`, one typed `ClosedEvent`, `NotOpen` from native mutations, `AlreadyOpen` before finalization, successful owner-thread `close()`, and reopen afterward.
7. Let a Window-owning thread exit while the portable object remains alive elsewhere. Verify the dispatcher restores exclusive state and destroys the HWND before the surviving object is released, with no duplicate cleanup or stale ID.

## Custom chrome

1. Validate dragging, edge/corner resizing, snap layouts, system menu, minimize, maximize/restore, and close regions.
2. Change layouts while visible and verify old rectangles stop responding immediately.
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

1. Exercise every standard cursor shape.
2. Validate hidden, confined, hidden-confined, and relative modes while focused.
3. Alt-tab away, minimize, hide, restore, and close while confined/relative; verify the system cursor is always released and exclusive relative centering resumes only while focused.
4. Warp to client corners and validate logical positions at multiple DPI scales.

## Files and shell behavior

1. Enable file drops and drag one file, multiple files, Unicode paths, and paths containing spaces; verify one grouped event and optional client position.
2. Disable drops and confirm no event is produced.
3. Validate custom icons at small/large shell sizes, attention flashing, focusability, disabled interaction, topmost toggling, and standard control disabling.
4. Exercise every valid resizable/maximizable combination and both invalid transition orders. Verify closable and minimizable remain independent.
5. Confirm an owned Window has no independent taskbar entry by default; remove and restore the owner and verify styles and taskbar behavior recover.

## Fullscreen and display topology

1. Enter and leave borderless fullscreen on each monitor; verify exact monitor bounds and saved windowed placement.
2. Enter an enumerated exclusive mode, alt-tab away and back, then leave and close; verify desktop mode restoration at every step.
3. Reject an unsupported exact mode without changing Window or display state.
4. Move between monitors with different DPI and verify logical client geometry, physical framebuffer extent, scale/DPI events, and current monitor.
5. Connect/disconnect or enable/disable a monitor where practical, re-enumerate after the display event, and verify stale monitor IDs fail cleanly.
6. Disconnect the active borderless and exclusive target. Verify exclusive display state is restored and the Window recovers visibly to windowed mode on the surviving primary monitor.
7. Verify recovery clears `FullscreenInfo` and orders events as display configuration, mode, optional monitor, optional geometry/framebuffer, then optional DPI/content scale. Confirm the pump reports any restoration/repositioning failure without leaving stale fullscreen state.

## Modern Windows capabilities

1. On Windows 11 build 22621 or newer, apply and clear every `BackdropEffect`; repeat on an older supported build and verify `Unsupported`.
2. On Windows 11 build 26100 or newer, validate `DWMWA_REDIRECTIONBITMAP_ALPHA` output using renderer-provided premultiplied alpha. Repeat on an older build and verify open returns `Unsupported` without a partial Window.
3. Confirm whole-window opacity remains independent from framebuffer alpha.

## Failure observations

For every failed checked operation, record the portable error code, native code, diagnostic text, `isOpen()`, and relevant cached properties. Verify the Window remains either unchanged and retryable or completely closed according to @ref window_lifecycle_and_events.

## Related pages

- @ref window_coordinates_and_dpi
- @ref window_fullscreen_and_monitors
- @ref window_testing
