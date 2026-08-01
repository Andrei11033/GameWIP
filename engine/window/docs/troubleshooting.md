@page window_troubleshooting Troubleshooting

## `InvalidArgument` during open

Check for a zero client dimension, invalid UTF-8 or embedded NUL in the title, inverted size limits, a zero aspect-ratio component, opacity outside `[0, 1]`, an unknown enum value, an exclusive display mode attached to a non-exclusive request, an unknown owner/monitor, or region pointer modes without a runtime layout. External event storage must be non-empty.

## `ResourceBusy`

Native operations, queue consumption, close, renderer publication, native handles, and event pumping belong to the opening thread. The Window cannot be moved to transfer affinity. Post work to the owner thread and use `wakeEventWait()` to interrupt its wait. Recursive event pumping also reports `ResourceBusy`.

## Focus request fails

Foreground activation is subject to operating-system policy. A hidden or non-focusable Window cannot be focused. Treat `requestFocus()` as a checked request, not an unconditional transfer.

## Events appear missing

Inspect `eventQueueInfo().droppedEvents`. Geometry and DPI events can coalesce. When full, the queue prefers evicting an older coalescible event; if it contains only noncoalescible events, a new event other than `ClosedEvent` is dropped. `ClosedEvent` evicts the oldest entry so unexpected native destruction remains observable. Cached getters remain current even when another notification was dropped.

`closeRequested()` is independent of the queue. Handle it even if no `CloseRequestedEvent` was retained.

## Fullscreen transition fails

Re-enumerate monitors and display modes immediately before the transition. IDs are process-local snapshots and a display reconfiguration can invalidate them. An exact exclusive request must match native resolution, refresh rate, color depth, and interlace state.

Explicit close restores exclusive display state. Do not terminate the process as a substitute for close during development; restoration then depends on operating-system recovery.

## Click-through behaves unexpectedly

Whole-window `ClickThrough` applies `WS_EX_LAYERED | WS_EX_TRANSPARENT`, including the native frame; returning to `Normal` restores ordinary hit testing. Opacity and click-through are independent.

Rectangular and per-pixel routing must reach arbitrary underlying desktop windows. Win32 currently reports `PointerRegions == false`, a zero region limit, and `Unsupported`; it does not use same-thread `HTTRANSPARENT`.

## Native destruction was unexpected

`isOpen()` becomes false while `lifetimeState()` reports `NativeDestroyedPendingFinalize`. Consume the retained `ClosedEvent`, stop native/renderer use, then call `close()` on the owner thread. Reopening is intentionally rejected until that controlled finalization releases IDs, event storage, and backend bookkeeping.

## Native handle is unavailable

Include `window/native/win32.h` only in a Win32 translation unit and query after successful open on the owner thread. `getHandle()` returns `ResourceBusy` on another thread and `NotOpen` without a live HWND. Never destroy the returned HWND.

## Occlusion reporting is unavailable

Global `supports(Types::Capability::OcclusionReporting)` is false by design: the native Window backend does not know whether a renderer-owned surface can present. Include `window/renderer.h` and attach one provider after surface creation.

Attachment, reporting, and detachment must run on the Window owner thread. `reportOcclusion()` returns `NotOpen` before attachment and after detachment. Forward only an authoritative Renderer presentation result; minimization, visibility, or focus alone are not equivalent to renderer occlusion.

## Display color is unknown or stale

Include `window/renderer.h` and perform the first color query on the Window owner thread if that thread should receive advanced-color transition signals. Keep pumping events and re-query after `MonitorChangedEvent` or `DisplayConfigurationChangedEvent`. Optional numeric fields legitimately remain zero when the operating system, display driver, or output interface does not expose reliable metadata; do not replace them with assumed panel defaults.

## Close reports failure

If `isOpen()` remains true, cleanup stopped before native destruction and the owning thread may retry. If `isOpen()` is false, destruction completed and the status is a late cleanup diagnostic. In both cases the object remains valid.

## `Unsupported` during open

On Windows, confirm the executable manifest declares Per-Monitor-V2 DPI awareness. Window validates the effective context and does not change process policy. System backdrops require Windows 11 build 22621, transparent framebuffer alpha requires build 26100, and rectangular/per-pixel pointer routing is not advertised by the current Win32 backend.

## Related pages

- @ref window_package_abi
- @ref window_coordinates_and_dpi
- @ref window_manual_validation
