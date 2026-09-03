@page desktop_drag_drop Native data drag and drop

`GameWIP::Desktop` provides native inter-application data drag and drop through
the opt-in `desktop/drag_drop.h` header. The feature supports GameWIP and foreign
applications in either direction without adding another library, worker thread,
or application callback. `desktop/window.h` does not include this optional API.

DragDrop reuses `Types::DataTransfer` from `desktop/data_transfer.h` rather than
defining a second payload model:

- strict UTF-8 `TextView` and owning `Text`;
- ordered absolute-path `FileListView` and owning `FileList`;
- top-to-bottom sRGB straight-alpha RGBA8 `ImageView` and owning `Image`;
- named opaque `CustomView` and owning `CustomData`.

The target side is an event-producing RAII resource. The source side is one
synchronous modal operation. Neither side depends on the Input library or
interprets modifier keys as effect policy.

## Target ownership and lifetime

`DragDropTarget` is closed by default, non-copyable, and non-movable. The public
owner holds stable heap state used by the native target registration. One full
target may be open for one open top-level `Window`; child surfaces are not
supported.

`open()` inherits the Window owner thread and offers the same queue-storage
choices as other Desktop resource owners:

| Overload | Storage |
| --- | --- |
| `open(window, description)` | Internal default-capacity queue. |
| `open(window, description, capacity)` | Internal queue with nonzero requested capacity. |
| `open(window, description, eventStorage)` | Nonempty caller-owned storage borrowed until close. |

Descriptions, regions, format names, and source data are copied. Caller views
are never retained. No DragDrop allocation, OLE initialization, native
registration, or permanent Window state exists until a target is opened or a
source drag begins.

Open, close, region replacement, and event consumption are owner-thread-only.
A wrong-thread explicit operation reports `ResourceBusy`; wrong-thread
destruction transfers cleanup to the existing Desktop owner-thread dispatcher.
Explicit close permits reopen. If the Window or native registration disappears,
the target loses native access, retains its Window lifetime identity, enters
`NativeDestroyedPendingFinalize`, and can be finalized with owner-thread
`close()`. A failed native revocation leaves registration, OLE ownership, and
the target state intact so owner-thread cleanup can be retried; it never detaches
the registered callback or balances OLE initialization prematurely. An ordinary
`Window::close()` therefore stops before destroying the HWND when target
revocation fails, leaving both owners retryable.

Owner-thread dispatcher termination is different because no later call can run
in that apartment. Before the dispatcher disappears, it finalizes every deferred
and active target on the owner thread. Surviving public owners retain their ended
Window identity and pending-finalization state, but no apartment-affine native
cleanup remains for them; a later `close()` only releases that portable state.
If native revocation repeatedly fails during this non-reporting shutdown path,
Desktop detaches its portable state while the operating system retains the
registered native callback for apartment/process teardown. This exceptional
fallback prevents later wrong-thread COM use and does not affect other targets.

## Regions and acceptance

`TargetDescription::regions` is the initial immutable region snapshot.
`setRegions()` validates and copies a complete replacement before publishing it,
so failure leaves the prior snapshot unchanged. Region IDs already present in
queued events remain ordinary values and are not invalidated by replacement.

Every region has:

- a nonzero unique `RegionId`;
- an optional logical client rectangle;
- an ordered list of accepted `DataTransfer::FormatView` values;
- a nonempty `allowedEffects` bitmask;
- one `preferredEffect` contained in that mask.

An absent rectangle means the whole client and follows Window resize
automatically. Explicit rectangles use logical Window-client coordinates.
Regions may overlap; the last supplied matching region wins. A position outside
all accepting regions has invalid `RegionId{}` and `Effect::None`.

Standard formats require an empty custom name. Custom formats require a
nonempty strict UTF-8 native name without embedded U+0000. Duplicate accepted
formats within one region are invalid. A region may accept several formats; a
successful Drop materializes every accepted format that the source offers, in
the region's declared order.

## Effect negotiation

Portable negotiation is deterministic and ignores Ctrl, Shift, Alt, and native
key-state fields:

```text
candidates = source allowedEffects & target allowedEffects
```

| Candidates | Selected effect |
| --- | --- |
| None | `Effect::None`; the target rejects. |
| Exactly one | That candidate. |
| Several and preferred is present | The target's `preferredEffect`. |
| Several without preferred | First available in Copy, Move, Link order. |

A source can force an operation by advertising one effect. GameWIP never
deletes, renames, or mutates source data itself. An external target may still
apply an advertised `Move` to referenced resources such as `FileList` paths;
advertise only `Copy` when those resources must remain unchanged, and use
disposable resources when validating foreign Move behavior.

## Target events and sessions

Each native visit receives a nonzero `SessionId`. Each successful target open
lifetime independently starts event `sequence` at one.

| Event | Meaning |
| --- | --- |
| `Entered` | The native session entered the top-level target. Carries position, current region, selected effect, and immutable offered-format metadata. |
| `Moved` | The position, region, or selected effect was observed again. Carries previous/current region IDs and reuses the session format snapshot. |
| `Left` | The session left the top-level target without a successful Drop. |
| `Dropped` | All selected payload items materialized successfully. Owns the complete `DataTransfer::Payload`. |

`Entered` and `Left` do not represent individual region boundaries. Moving from
region A to B produces `Moved{previousRegion=A, region=B}`, not a synthetic
Left/Entered pair.

Drag enter and movement enumerate bounded format metadata but do not retrieve
payload-sized data. Final Drop retrieval is synchronous and transactional. If
one selected format is unavailable, malformed, unsupported, fails conversion,
or cannot allocate, the target reports `Effect::None` and queues no partial
`Dropped` event. A foreign provider can block while servicing the final native
request; Desktop does not hide that behavior behind a thread.

## Queue behavior

The target queue has fixed capacity and exposes `Types::Events::QueueInfo`.
Compatible adjacent `Moved` events from one session coalesce while preserving
the earliest previous region and newest useful state. Other event kinds do not
coalesce.

When full, ordinary incoming noise is counted and dropped. A final accepted
`Dropped` event instead evicts older movement state when possible, or the oldest
retained event otherwise. This guarantees that old movement noise cannot
silently discard the successful terminal payload. `popEvent()`, `popEvents()`,
`clearEvents()`, and close replace vacated slots so shared format metadata and
large payload allocations are released immediately. Terminal insertion uses
fixed-capacity queue bookkeeping and does not shift the caller-sized queue in a
native Drop callback.

## Target example

```cpp
#include "desktop/drag_drop.h"

#include <array>
#include <optional>

namespace Desktop = GameWIP::Desktop;
namespace DD = GameWIP::Desktop::Types::DragDrop;
namespace Transfer = GameWIP::Desktop::Types::DataTransfer;

std::array accepted{
    Transfer::FormatView{Transfer::FormatKind::Text, {}},
    Transfer::FormatView{Transfer::FormatKind::FileList, {}}};

std::array regions{
    DD::RegionDescription{
        DD::RegionId{1},
        std::nullopt,
        accepted,
        DD::Effect::Copy | DD::Effect::Move,
        DD::Effect::Copy}};

Desktop::DragDropTarget target;
const auto opened = target.open(window, DD::TargetDescription{regions});
if (!opened.ok())
{
    // Inspect opened.code/nativeCode; the target remains closed.
}

DD::Event event;
while (target.popEvent(event))
{
    if (const auto *drop = event.getIf<DD::Events::Dropped>())
    {
        // drop->payload owns every successfully materialized item.
    }
}
```

Pump the normal shared `Desktop::Events::poll()` or `wait()` loop so native
Window messages remain coherent. The target owns and exposes its own event
queue; no separate DragDrop pump exists.

## Source operation

`DragDrop::beginDrag(source, description)` requires an open source Window owned
by the calling thread. `Description` supplies a nonempty ordered item list,
nonempty valid allowed-effects mask, and explicit `TriggerButton::{Left, Right,
Middle}`.

The configured button must already be down when the function begins; Desktop
does not infer it. Before entering native ownership, the function validates all
items, rejects duplicate native formats, copies caller data, performs strict
Unicode/path/image conversion, and prepares immutable native wire blocks. A
preparation failure enters no modal drag and retains no caller memory.

`beginDrag()` then enters synchronous Win32 `DoDragDrop`:

- release of the configured trigger requests completion/drop;
- Escape cancels;
- unrelated mouse-button changes do not terminate the drag;
- a nested source drag on the same owner thread reports `ResourceBusy`.

Foreign consumers may request one prepared format repeatedly. Each request gets
fresh native storage copied from the immutable preparation; no caller access,
conversion, file IO, renderer work, or application callback occurs at that
time. The source Window must remain open for the complete call.

```cpp
Transfer::TextView text{"Drag me"};
std::array<Transfer::ItemView, 1> items{text};

DD::Description description{
    items,
    DD::Effect::Copy,
    DD::TriggerButton::Left};

const DD::Result result = Desktop::DragDrop::beginDrag(window, description);
if (result.status.ok() && result.outcome == DD::Outcome::Dropped)
{
    // result.effect is the performed Copy/Move/Link operation.
}
```

Cancellation is not an IO error: it returns successful status,
`Outcome::Cancelled`, and `Effect::None`. An accepted completion returns
successful status, `Outcome::Dropped`, and the performed effect. Setup,
conversion, allocation, OLE, and native failures use failed status.

## Lightweight file-drop migration

The existing `Window::fileDropEnabled` mode and `Types::Events::FilesDropped`
remain the small path-only API. It uses different native registration and is
mutually exclusive with a full target:

- opening `DragDropTarget` while lightweight mode is enabled returns
  `ResourceBusy`;
- enabling lightweight mode while a target is active returns `ResourceBusy`;
- neither operation silently disables the other.

Applications needing effects, regions, text, images, custom formats, or source
dragging should disable lightweight mode explicitly, open `DragDropTarget`, and
consume its typed queue.

## Win32 OLE and format mapping

OLE initialization is acquired lazily on the owner thread and balanced when the
target/source lifetime ends. A compatible existing apartment is reused. An
incompatible apartment returns `ResourceBusy`; Desktop does not move the operation
to a hidden STA thread.

| Portable format | Win32 representation |
| --- | --- |
| Text | `CF_UNICODETEXT` with strict conversion and native terminator. |
| File list | Unicode `CF_HDROP`; paths are copied without file IO. |
| Image | Top-down `CF_DIBV5` source; common `CF_DIB`/DIBV5 target materialization. |
| Custom | Case-insensitive registered native clipboard-format identity. |

Clipboard and DragDrop share private Win32 transfer preparation and
materialization machinery while retaining different ownership and transaction
policies. Enumeration and materialization from foreign providers use
implementation defensive ceilings and checked arithmetic. Those ceilings are
not public portable limits.

Immediate zero-byte custom publication is not representable faithfully through
the current Win32 movable-`HGLOBAL` producer. It returns `Unsupported` rather
than changing the payload extent or opting into delayed rendering.

## Status guide

| Status | Typical meaning |
| --- | --- |
| `InvalidArgument` | Zero/duplicate ID, invalid effect/preference, bad format/name/data, empty queue storage/items, or trigger not held. |
| `NotOpen` | Source/target Window is closed, or target native access was lost. |
| `AlreadyOpen` | The same target owner already retains a lifetime. |
| `ResourceBusy` | Wrong owner thread, another target/lightweight mode, nested source drag, or incompatible apartment. |
| `Unsupported` | A valid portable request cannot be represented by the current native path. |
| `OutOfMemory` | Region, queue, payload, preparation, or native storage allocation failed. |
| `OpenFailed` / `CloseFailed` / `NativeFailure` | OLE registration, revocation, modal operation, or other native setup/cleanup failed. |

Checked public operations are `noexcept`. A failed `open()` leaves the target
closed; failed transactional region replacement preserves the old snapshot;
failed final Drop publishes no partial payload.

## Related pages

- @ref desktop_clipboard
- @ref desktop_lifecycle_events
- @ref desktop_testing
- @ref desktop_troubleshooting
- @ref desktop_manual_validation
- @ref desktop_package_abi
