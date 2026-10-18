@page desktop_troubleshooting Troubleshooting

Most failures identify a lifecycle, owner-thread, capability, or native-state boundary. Start with
the returned status and `lifetimeState()`, then find the case below.

## A dialog returns ResourceBusy, NotOpen, or Unsupported

A non-null owner must be a live native Window and the call must run on its owner thread: a lost or closed owner gives @c NotOpen, a
foreign one @c ResourceBusy. Shell file/folder dialogs also need an STA-compatible COM
apartment; an existing MTA or neutral apartment gives @c ResourceBusy (no hidden dialog thread). @c Unsupported means the
backend cannot preserve the exact native semantics and does not substitute a weaker one.

Progress mutation and close are owner-thread operations. Keep pumping
@c Desktop::Events, hand worker results to that thread, and treat @c hasCancelRequest() as
sticky intent, not proof that work stopped. See @ref desktop_dialogs.

## DragDrop target or source is busy

For target open, check `Window::fileDropEnabled()`, owner-thread affinity, and
whether another `DragDropTarget` owns the Window. File drop and `DragDropTarget`
are mutually exclusive; see @ref desktop_drag_drop.

An incompatible COM apartment also fails (no hidden STA thread). `beginDrag()` reports `ResourceBusy` for a nested source drag on
the owner thread; post later work after the synchronous call returns.

## DragDrop source reports `InvalidArgument`

The source Window must be open on the calling thread; `items` non-empty; `effects.allowed` only `Copy`, `Move`, `Link` and not
`None`; `effects.preferred` one of the allowed effects; formats unique; every item valid per
@ref desktop_clipboard format validation. The explicit `Left`, `Right`, or
`Middle` trigger button must already be down when `beginDrag()` starts; Desktop does not infer it.

## A target region never accepts

Confirm the ID is nonzero and unique, the custom name is exact strict UTF-8,
the region accepts at least one offered portable format, and source/target
effect policies intersect. Explicit rectangles use logical client coordinates;
an absent rectangle means the complete current client. In overlap, the last
matching supplied region wins.

Modifier keys do not change the negotiated portable effect. Configure the
drag source/region `EffectPolicy` instead; its `allowed` mask controls what is
permitted and its `preferred` value controls deterministic selection.

## DragDrop events or payloads appear missing

Inspect the target's own `eventQueueInfo()`, not the Window queue. Compatible
movement events can coalesce, and overflow increments `droppedEvents`. A final
accepted `Dropped` has priority over retained movement noise.

No `Dropped` is queued when any selected native format is malformed,
unavailable, unsupported, or fails conversion/allocation (all-or-nothing, so no partial payload looks successful). Final retrieval
can block on a foreign synchronous provider; there is no worker thread or
portable cancellation once retrieval begins.

## DragDrop target lost its Window

When the Window or registration is destroyed, target `isOpen()` becomes false
and `lifetimeState()` reports `NativeDestroyedPendingFinalize`. Stop consuming
native session state and call target `close()` on the owner thread; reopen only after it succeeds.
`windowId()` keeps describing the ended Window lifetime until finalization.

If the owner thread already exited, its dispatcher finalized native
DragDrop resources, so a surviving public owner may be closed or
destroyed from its current thread. See @ref desktop_drag_drop.

## DragDrop custom data or image is rejected

Custom producer names use Win32 registered-format identity and need an
agreed schema. An exact zero-byte custom source block is `Unsupported` (immediate movable-`HGLOBAL` path). Images need positive dimensions,
valid checked stride/extent, top-to-bottom RGBA8 input, and a DIB layout
the receiver can represent. Malformed foreign DIB masks or extents reject the
whole Drop.

## `InvalidArgument` during open

Check for a zero client dimension, invalid UTF-8 or embedded NUL in the title, inverted size limits, a zero aspect-ratio component, opacity outside
`[0, 1]`, an unknown enum value, an exclusive display mode on a non-exclusive request, an unknown owner/monitor, region pointer modes
without a runtime layout, or empty external event storage.

Also check option combinations: a hidden Window cannot request focus or an
initial minimized/maximized presentation, a non-resizable Window cannot
keep the maximize control enabled, and the initial client size must satisfy its
limits. See @ref desktop_public_api.

## `ResourceBusy`

Native operations, queue consumption, close, renderer publication, native handles, and event pumping belong to the opening thread, and a Window
cannot be moved to transfer affinity. Post work to the owner thread and interrupt its wait with `wakeEventWait()` (only while the open lifetime is
stable; synchronize with close and destruction; it makes no other operation thread-safe). Recursive event pumping also reports
`ResourceBusy`. See @ref desktop_lifecycle_events.

## Shell interactions never reach the queue

Opening a shell resource and consuming `ShellEventQueue` do not dispatch
native messages. Keep calling `Desktop::Events::poll()` or `wait()` on
the resource owner thread, even with no top-level Window.
Check that the resource has a queue and it is still open; passive tray
icons and notification centers discard interactions without one.

Pump counts cover Window and ChildSurface events only; inspect the
shell queue's `eventQueueInfo()` for shell pressure. Release every bound
resource before closing its borrowed queue. See @ref desktop_shell.

## Focus request fails

Foreground activation is subject to operating-system policy. A hidden or non-focusable Window cannot be focused. Treat `requestFocus()` as a checked
request, not an unconditional transfer.

## Events appear missing

Inspect `eventQueueInfo().droppedEvents`. Geometry and DPI events can coalesce. When full, the queue prefers evicting an older coalescible event; if
it contains only noncoalescible events, a new event other than `Types::Events::NativeDestroyed` is dropped. `NativeDestroyed` evicts the oldest entry
so unexpected native destruction remains observable. Cached getters remain current even when another notification was dropped.

`hasCloseRequest()` is independent of the queue. Handle it even if no `Types::Events::CloseRequested` payload was retained.

## Fullscreen transition fails

Re-enumerate monitors and display modes immediately before the transition. IDs are process-local snapshots and a display reconfiguration can
invalidate them. An exact exclusive request must match native resolution, refresh rate, color depth, and interlace state.

Explicit close restores exclusive display state. Do not terminate the process as a substitute for close during development; restoration then depends
on operating-system recovery.

## Click-through behaves unexpectedly

Whole-window `ClickThrough` applies `WS_EX_LAYERED | WS_EX_TRANSPARENT`, including the native frame; returning to `Normal` restores ordinary hit
testing. Opacity and click-through are independent.

Rectangular and per-pixel routing must reach arbitrary underlying desktop windows. Win32 reports `PointerRegions == false`, a zero region
limit, and `Unsupported`; it does not use same-thread `HTTRANSPARENT`.

## Native destruction was unexpected

`isOpen()` is false and `lifetimeState()` reports `NativeDestroyedPendingFinalize`. Consume the retained `Types::Events::NativeDestroyed`
payload, stop native/renderer use, then call `close()` on the owner thread; reopen is rejected until then. See @ref desktop_lifecycle_events.

## Native handle is unavailable

Include `desktop/native/win32.h` only in a Win32 translation unit and query after successful open on the owner thread. `getHandle()` returns
`ResourceBusy` on another thread and `NotOpen` without a live HWND. Never destroy the returned HWND.

For ChildSurface hosting, include `desktop/child_surface.h` and `desktop/native/win32.h`; the
external technology must not destroy, reparent, or subclass the host. See @ref
desktop_child_surfaces.

## Clipboard reports ResourceBusy

Another process or thread owns clipboard access. Use the explicit timeout overload when the operation has a different latency budget;
`Clipboard::kNoWait` performs one attempt and the convenience overload uses `kDefaultAccessTimeout`. Retrying remains bounded. A timeout cannot cancel
a foreign delayed-rendering call already entered by Windows.

## Clipboard write failed after changing contents

Inspect `WriteResult::commitState` and `formatsPublished`. `Cleared` means the old contents are gone but the first requested format failed;
`PartiallyPublished` means the reported caller-order prefix is externally visible. A close failure may accompany `Published` because cleanup failure
does not erase a completed side effect. Use `clear()` only for intentional clearing.

## Clipboard custom data has an unexpected extent

GameWIP returns the opaque native allocation extent and adds no prefix or framing. Another application's allocation may include padding. The custom
format specification must define any logical length field. Names follow case-insensitive Win32 registered-format identity. Immediate zero-byte custom
publication is `Unsupported` on Win32 because a zero-sized movable allocation is discarded; using `nullptr` would require out-of-scope delayed
rendering.

## Clipboard text, file, or image input is rejected

Text must be strict UTF-8 and Win32 text cannot preserve embedded U+0000. File lists must be non-empty absolute paths, but paths need not exist and no
file-system query occurs. Image dimensions must be positive, a packed image
uses `ImageView::packed()` and a padded image uses `ImageView::strided()`, the
stride must hold `width * 4` bytes, and the byte span must equal the resolved
stride times height exactly. See @ref desktop_clipboard.

## Custom cursor selection fails

Include `desktop/cursor.h` and check `Types::Capability::CustomCursor`. `createCursor()` returns `InvalidArgument` for an empty variant set, invalid
dimensions or hotspot, zero or duplicate intended DPI, an undersized stride, or a payload whose size does not exactly match its resolved rows.
`setCursor()` additionally requires a valid `Cursor` and an open Window on its owner thread. See @ref desktop_custom_cursors for the complete
contract.

## Occlusion reporting is unavailable

`supports(Types::Capability::OcclusionReporting)` is the stable backend capability, not provider
attachment. Include `desktop/renderer_bridge.h`, check `Renderer::hasOcclusionProvider(window)`,
and call
`Renderer::attachOcclusionProvider(window)` once the renderer has an authoritative presentation source.

All three occlusion calls run on the owner thread. `reportOcclusion()` returns `NotOpen` before attachment and after
detachment. Forward only an authoritative Renderer result; minimization, visibility, or focus are not occlusion.

## Concurrent presentation reads are unavailable

Call `Renderer::enableConcurrentPresentationReads(window)` on the open Window's owner thread before starting
the renderer thread. `NotOpen`: no native lifetime committed; `ResourceBusy`: not the owner thread; `OutOfMemory`:
publication allocation not installed. A failed call leaves owner-thread getters unchanged and the
feature disabled. Do not retry from a renderer getter. See @ref desktop_renderer_integration.

## Display color is unknown or stale

Include `desktop/display_info.h` and perform the first `Display::getColorInfo(...)` query on the Window owner thread if that thread should receive
advanced-color transition signals. Keep pumping events and re-query after `Types::Events::MonitorChanged` or
`Types::Events::DisplayConfigurationChanged`. Optional numeric fields legitimately remain zero when the operating system, display driver, or output
interface does not expose reliable metadata; do not replace them with assumed panel defaults.

## Close reports failure

If `isOpen()` remains true, cleanup stopped before native destruction and the owning thread may retry. If `isOpen()` is false, destruction completed
and the status is a late cleanup diagnostic. In both cases the object remains valid.

## `Unsupported` during open

On Windows, confirm the application called `gamewip_attach_application_manifest(TARGET <executable> PER_MONITOR_V2)` and that its single executable
manifest declares Per-Monitor-V2 DPI awareness. Window validates the effective
context and does not change process policy. System backdrops require Windows 11
build 22621, transparent framebuffer alpha requires build 26100, and
rectangular/per-pixel pointer routing is not advertised by the current Win32
backend.

## Accessibility tree or actions are unavailable

Include `desktop/accessibility.h`, enable after Window open, and publish a valid
complete snapshot. A bridge without a snapshot does not expose a semantic tree.
Validation/preparation failure preserves the old generation; inspect its status
and `Accessibility::validate()` issue. Generations remain strictly increasing
across reopen and root identity stays fixed within activation. Provider identity
capacity covers all distinct IDs submitted during an activation, not just the
current tree; do not churn IDs unnecessarily.

Native patterns require matching metadata/actions and unredacted content. A
disabled host, including a native owner blocker, rejects actions even if a node
does not declare Disabled. Drain actions on the opening thread and apply them
in application code. A full action queue rejects the newest request; a full
notification queue invalidates the latest tree without undoing publication.

Keep pumping owner events for notifications. Announcement support and listening
clients are independent; Win32 rejects differing per-message language overrides.
Ranges become unavailable after any newer generation; reacquire them, release
obsolete ranges/readers, and supply fragments rather than expecting Desktop to
infer glyph layout. Retained providers after close/disable cannot revive in a
new activation. See @ref desktop_accessibility and the manual client workflow.

## Related pages

- @ref desktop_package_abi
- @ref desktop_coordinates_and_dpi
- @ref desktop_renderer_integration
- @ref desktop_manual_validation
- @ref desktop_clipboard
- @ref desktop_drag_drop
