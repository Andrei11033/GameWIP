@page desktop_troubleshooting Troubleshooting

Most Window failures identify a lifecycle, owner-thread, capability, or native
state boundary. Start with the returned status and `lifetimeState()`, then use
the matching case below.

## `InvalidArgument` during open

Check for a zero client dimension, invalid UTF-8 or embedded NUL in the title, inverted size limits, a zero aspect-ratio component, opacity outside
`[0, 1]`, an unknown enum value, an exclusive display mode attached to a non-exclusive request, an unknown owner/monitor, or region pointer modes
without a runtime layout. External event storage must be non-empty.

## `ResourceBusy`

Native operations, queue consumption, close, renderer publication, native handles, and event pumping belong to the opening thread. The Window cannot
be moved to transfer affinity. Post work to the owner thread and use `wakeEventWait()` to interrupt its wait. Recursive event pumping also reports
`ResourceBusy`.

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

Rectangular and per-pixel routing must reach arbitrary underlying desktop windows. Win32 currently reports `PointerRegions == false`, a zero region
limit, and `Unsupported`; it does not use same-thread `HTTRANSPARENT`.

## Native destruction was unexpected

`isOpen()` becomes false while `lifetimeState()` reports `NativeDestroyedPendingFinalize`. Consume the retained `Types::Events::NativeDestroyed`
payload, stop native/renderer use, then call `close()` on the owner thread. Reopening is intentionally rejected until that controlled finalization
releases IDs, event storage, and backend bookkeeping.

## Native handle is unavailable

Include `desktop/native/win32.h` only in a Win32 translation unit and query after successful open on the owner thread. `getHandle()` returns
`ResourceBusy` on another thread and `NotOpen` without a live HWND. Never destroy the returned HWND.

For ChildSurface native hosting, include both `desktop/child_surface.h` and `desktop/native/win32.h`. The external technology may own descendants but
must not destroy, reparent, or subclass the GameWIP host. Shut the external technology down before closing the ChildSurface when its SDK requires it.

## Clipboard reports ResourceBusy

Another process or thread currently owns clipboard access. Use the explicit timeout overload when the operation has a different latency budget;
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

Text must be strict UTF-8 and Win32 text cannot preserve embedded U+0000. File lists must be nonempty absolute paths, but paths need not exist and no
file-system query occurs. Image dimensions must be positive, stride must hold `width * 4` bytes, and the byte span must equal the resolved stride
times height exactly. See @ref desktop_clipboard.

## Custom cursor selection fails

Include `desktop/cursor.h` and check `Types::Capability::CustomCursor`. `createCursor()` returns `InvalidArgument` for an empty variant set, invalid
dimensions or hotspot, zero or duplicate intended DPI, an undersized stride, or a payload whose size does not exactly match its resolved rows.
`setCursor()` additionally requires a valid `Cursor` and an open Window on its owner thread. See @ref desktop_custom_cursors for the complete
contract.

## Occlusion reporting is unavailable

`supports(Types::Capability::OcclusionReporting)` describes the stable backend capability; it does not report whether a renderer provider is currently
attached. Include `desktop/renderer_bridge.h`, check `Renderer::hasOcclusionProvider(window)` for attachment state, and call
`Renderer::attachOcclusionProvider(window)` after the renderer has an authoritative presentation source.

Attachment, reporting, and detachment must run on the Window owner thread. `reportOcclusion()` returns `NotOpen` before attachment and after
detachment. Forward only an authoritative Renderer presentation result; minimization, visibility, or focus alone are not equivalent to renderer
occlusion.

## Concurrent presentation reads are unavailable

Include `desktop/renderer_bridge.h` and call `Renderer::enableConcurrentPresentationReads(window)` on the open Window's owner thread before starting
the renderer thread. `NotOpen` means no native lifetime is committed, `ResourceBusy` means the caller is not the owner thread, and `OutOfMemory`
means the lazy publication allocation was not installed. A failed call leaves ordinary owner-thread getters unchanged and the feature disabled.

Do not retry enablement from a renderer getter or race enablement with reads. After success, enablement is idempotent, remains active across
close/reopen, and has no disable operation. Stop or join renderer reads before destroying the C++ `Window` object.

## Display color is unknown or stale

Include `desktop/display_info.h` and perform the first `Display::getColorInfo(...)` query on the Window owner thread if that thread should receive
advanced-color transition signals. Keep pumping events and re-query after `Types::Events::MonitorChanged` or
`Types::Events::DisplayConfigurationChanged`. Optional numeric fields legitimately remain zero when the operating system, display driver, or output
interface does not expose reliable metadata; do not replace them with assumed panel defaults.

## Close reports failure

If `isOpen()` remains true, cleanup stopped before native destruction and the owning thread may retry. If `isOpen()` is false, destruction completed
and the status is a late cleanup diagnostic. In both cases the object remains valid.

## `Unsupported` during open

On Windows, confirm the executable manifest declares Per-Monitor-V2 DPI awareness. Window validates the effective context and does not change process
policy. System backdrops require Windows 11 build 22621, transparent framebuffer alpha requires build 26100, and rectangular/per-pixel pointer routing
is not advertised by the current Win32 backend.

## Related pages

- @ref desktop_package_abi
- @ref desktop_coordinates_and_dpi
- @ref desktop_renderer_integration
- @ref desktop_manual_validation
- @ref desktop_clipboard
