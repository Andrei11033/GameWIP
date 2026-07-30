@page window_troubleshooting Troubleshooting

## `InvalidArgument` during open

Check for a zero client dimension, invalid UTF-8 or embedded NUL in the title, inverted size limits, a zero aspect-ratio component, opacity outside `[0, 1]`, an unknown enum value, an exclusive display mode attached to a non-exclusive request, an unknown owner/monitor, or region pointer modes without a runtime layout. External event storage must be non-empty.

## `ResourceBusy`

Native operations, queue consumption, close, and event pumping belong to the opening thread. Move construction does not change that affinity. Post work to the owning thread and use `wakeEventWait()` to interrupt its wait. Recursive event pumping also reports `ResourceBusy`.

## Focus request fails

Foreground activation is subject to operating-system policy. A hidden or non-focusable Window cannot be focused. Treat `requestFocus()` as a checked request, not an unconditional transfer.

## Events appear missing

Inspect `eventQueueInfo().droppedEvents`. Geometry and DPI events can coalesce. When full, the queue prefers evicting an older coalescible event; if it contains only noncoalescible events, the new event is dropped. Cached getters remain current even when a notification was dropped.

`closeRequested()` is independent of the queue. Handle it even if no `CloseRequestedEvent` was retained.

## Fullscreen transition fails

Re-enumerate monitors and display modes immediately before the transition. IDs are process-local snapshots and a display reconfiguration can invalidate them. An exact exclusive request must match native resolution, refresh rate, color depth, and interlace state.

Explicit close restores exclusive display state. Do not terminate the process as a substitute for close during development; restoration then depends on operating-system recovery.

## Click-through behaves unexpectedly

All rectangles are logical client coordinates. Region modes require at least one region. Custom caption/system-button and resize hit tests intentionally take precedence over pointer pass-through. Opacity and click-through are independent settings.

## Native handle is unavailable

Include `window/native/win32.h` only in a Win32 translation unit and query after successful open. `getHandle()` returns `NotOpen` after close and on a default/moved-from object. Never destroy the returned HWND.

## Occlusion reporting is unavailable

Global `supports(Types::Capability::OcclusionReporting)` is false by design: the native Window backend does not know whether a renderer-owned surface can present. Include `window/integration/renderer_feedback.h` and attach one provider after surface creation. The individual Window advertises the capability only while that provider is attached.

Attachment, reporting, and detachment must run on the Window owner thread. `reportOcclusion()` returns `NotOpen` before attachment and after detachment. Forward only an authoritative Renderer presentation result; minimization, visibility, or focus alone are not equivalent to renderer occlusion.

## Close reports failure

If `isOpen()` remains true, cleanup stopped before native destruction and the owning thread may retry. If `isOpen()` is false, destruction completed and the status is a late cleanup diagnostic. In both cases the object remains valid.
