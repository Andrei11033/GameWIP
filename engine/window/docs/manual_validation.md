@page window_manual_validation Manual validation scenarios

Run these scenarios in a normal interactive desktop session. Record OS version, display topology, scale factors, GPU/driver, and pass/fail notes. Always return exclusive modes, cursor confinement, topmost policy, and opacity to normal before ending a scenario.

## Lifecycle and multiple windows

1. Create a visible focused Window, resize and move it, minimize/maximize/restore it, request close through the system button, decline once with `clearCloseRequest()`, request again, and explicitly close.
2. Open two independent Windows and pump both from one thread without WindowManager. Verify events route to the correct queue and closing one leaves the other operational.
3. Open an owned tool Window, activate and close it, change/remove its owner at runtime, and verify z-order/minimization behavior remains native and stable.

## Custom chrome

1. Validate dragging, edge/corner resizing, snap layouts, system menu, minimize, maximize/restore, and close regions.
2. Change layouts while visible and verify old rectangles stop responding immediately.
3. Repeat at 100%, 125%, 150%, and 200% scale and after moving between differently scaled monitors.

## Layered and pointer behavior

1. Exercise opacity at 1.0, intermediate values, and 0.0; verify input behavior is unchanged.
2. Create a transparent-framebuffer Window and verify compositor transparency and redraw behavior.
3. Validate complete click-through, accept-only rectangles, and ignore rectangles against another interactive application below.
4. Combine custom caption buttons with click-through and verify caption/resize precedence.

## Cursor

1. Exercise every standard cursor shape.
2. Validate hidden, confined, hidden-confined, and relative modes while focused.
3. Alt-tab away, minimize, hide, restore, and close while confined/relative; verify the system cursor is always released and exclusive relative centering resumes only while focused.
4. Warp to client corners and validate logical positions at multiple DPI scales.

## Files and shell behavior

1. Enable file drops and drag one file, multiple files, Unicode paths, and paths containing spaces; verify one grouped event and optional client position.
2. Disable drops and confirm no event is produced.
3. Validate custom icons at small/large shell sizes, attention flashing, focusability, disabled interaction, topmost toggling, and standard control disabling.

## Fullscreen and display topology

1. Enter and leave borderless fullscreen on each monitor; verify exact monitor bounds and saved windowed placement.
2. Enter an enumerated exclusive mode, alt-tab away and back, then leave and close; verify desktop mode restoration at every step.
3. Reject an unsupported exact mode without changing Window or display state.
4. Move between monitors with different DPI and verify logical client geometry, physical framebuffer extent, scale/DPI events, and current monitor.
5. Connect/disconnect or enable/disable a monitor where practical, re-enumerate after the display event, and verify stale monitor IDs fail cleanly.

## Failure observations

For every failed checked operation, record the portable error code, native code, diagnostic text, `isOpen()`, and relevant cached properties. Verify the Window remains either unchanged and retryable or completely closed according to @ref window_lifecycle_and_events.
