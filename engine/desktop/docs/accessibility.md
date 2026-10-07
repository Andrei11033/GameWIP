@page desktop_accessibility Accessibility snapshots and native providers

`desktop/accessibility.h` exposes application-supplied semantic snapshots to
native accessibility clients. Applications or a UI library own semantics,
layout, text, selection, and action execution. Desktop copies immutable
snapshots, provides native providers, queues actions and notifications within
configured limits, and handles teardown.

## Ownership and activation

Values live under `Desktop::Types::Accessibility`; `validate()`, `SnapshotBuilder`,
`SnapshotReader`, and `Facade` live under `Desktop::Accessibility`.
`Window::accessibility()` returns a non-owning façade by value. Include the
focused header and link `GameWIP::Desktop`; no additional package is needed.
Ordinary Windows allocate no bridge storage. Open the Window, check
`Capability::Accessibility`, and explicitly `enable()` on its opening thread.
`Options` fixes limits for the activation. Repeated enable returns `AlreadyOpen`.
Disable, close, and native destruction immediately gate providers and clear
active snapshots and queues. Reopening does not automatically enable the bridge.

Node providers are stable per `NodeId` during an activation. Removed nodes return
unavailable; resubmitting that ID restores the same provider, so never reuse it
for a different logical control. Old providers cannot revive across activation
or reopen boundaries. The root ID is fixed during an activation. Generations are
nonzero and strictly newer across the entire C++ Window object's lifetime,
including disable/re-enable and reopen. Failed publication consumes no generation.

## Thread contract

| Operation                                   | Thread and lifetime                                                                                  |
| ------------------------------------------- | ---------------------------------------------------------------------------------------------------- |
| `enable()`, `disable()`, `popAction()`      | Opening thread; enable finishes before sharing the façade.                                           |
| `publish()`, `announce()`, reads, telemetry | Any thread after enable while the borrowed Window object lives.                                      |
| `SnapshotBuilder`                           | Caller-owned, externally synchronized authoring storage.                                             |
| Independent `SnapshotReader` copies         | Any thread; immutable retention survives publication, disable, close, and Window destruction.        |
| Native queries                              | Copied snapshots and coherent cached geometry, without application callbacks or application mutexes. |
| Native notifications                        | Opening-thread `Events::poll()` / `Events::wait()`; no hidden dispatcher thread.                     |

Join façade users before destroying the borrowed Window. Publication can race
owner-thread close, but a closed activation cannot be resurrected or replace a
later activation. Readers retain original application content, not a redacted
native export: only pass them to application code authorized to inspect it.

## Complete snapshots

`SnapshotView` borrows the complete tree and every nested string/span during
validation/publication. Nodes may have arbitrary storage order; ordered children
define semantic traversal. IDs are unique and nonzero, the root has no parent,
every other node has exactly its declared parent, and every node is reachable.
Relations, selection, annotations, and extension references must target nodes
in the same snapshot. Relations have distinct kinds and distinct nonempty targets.

`SnapshotBuilder::addNode()` deeply copies active payloads, including nested
extension lists, and rejects duplicate IDs or copy-budget violations. Full
validation happens in `validate()` / `publish()`, allowing forward references
while authoring. Builder views expire on mutation/destruction.

Use `setGeneration()` and `setRoot()` to set publication metadata, then add
nodes in any storage order. `nodeCount()` and `limits()` inspect the builder.
`clear()` removes nodes and resets generation/root while retaining its limits.
`publish(builder)` uses the same validation and copy contract as
`publish(SnapshotView)`.

`readSnapshot()` retains without copying; `SnapshotReader::isValid()` reports
whether it retains a snapshot. `copySnapshot()` deeply copies and preserves its
destination on failure.
`snapshotInfo()` describes the active generation without retaining the tree.
For a retained reader, `info()` describes its own generation, `view()` exposes
the complete borrowed tree, and `find(NodeId)` performs allocation-free lookup.
Keep a reader alive while using any pointers or views obtained from it.

Publication validates, copies, indexes Unicode text,
computes native metadata, and prepares providers before atomic replacement.
Invalid UTF-8, unknown enum values/flags, dangling references, invalid geometry,
invalid values/ranges, and resource failure leave the old tree intact.
`ValidationResult` identifies the first semantic issue and source node.

## Geometry and host state

`Geometry` supplies node-local bounds and a direct affine transform into
Window-client logical units, not a parent-relative transform. Clipping rectangles
are already in Window-client logical units. Ancestor clips and visibility are
inherited. Native bounds are transformed axis-aligned enclosures, clipped to
those rectangles and the client area, then converted to physical virtual-screen
coordinates using cached origin and scale. The root can use host client bounds;
missing descendant geometry is not fabricated.

Hit testing uses inverse transforms. Higher `hitTestOrder` wins; ties use later
semantic preorder. Non-hit-testable decorative geometry can be singular.
Geometry, projected bounds, and fragments must be finite with nonnegative
extents. Move, size, DPI, focus, visibility, minimization, and interaction changes
refresh host caches independently of semantic generations. Disabled interaction,
including a dialog owner blocker, rejects actions independently of node state.

## Win32 pattern mapping

The backend exposes UI Automation through `WM_GETOBJECT` for `UiaRootObjectId`,
not MSAA. Roles map to the closest standard control type; unmatched roles use
document/group/custom fallbacks. Roles alone do not promise every pattern.

| UIA pattern                            | Portable input                                                                    |
| -------------------------------------- | --------------------------------------------------------------------------------- |
| Invoke                                 | `Invoke` action.                                                                  |
| Value / RangeValue                     | String value or numeric range; writes require `SetValue` and non-read-only state. |
| Toggle / ExpandCollapse                | Check/pressed or expandable state and corresponding actions.                      |
| Selection / Selection2 / SelectionItem | Explicit selection policy, selected descendants, selectable state and actions.    |
| Scroll / ScrollItem                    | Scroll percentages/view sizes and typed scrolling actions.                        |
| Grid / GridItem / Table / TableItem    | Table dimensions, descendant indexes/spans, optional header relations.            |
| VirtualizedItem / ItemContainer        | Copied known virtualized nodes, `Realize`, bounded known-descendant search.       |
| Text / Text2 / TextRange               | Unredacted text; explicit caret for Text2 caret ranges.                           |
| Annotation / TextChild                 | Explicit text annotation target and range association.                            |

Check `features()` after activation and per-node native pattern availability.
Some portable concepts lack standard UIA properties. Namespaced typed extensions
and unmappable relations remain inspectable through portable readers; no custom
native property registration or fabricated controls are performed. Multiple
annotation associations remain copied; native Annotation/TextChild uses the
first submitted association. There is no Window/Transform pattern for mutating
the host, no TextEdit, and no loader creating unknown virtual nodes. Handle
`Realize` in application code, then publish new state. Unknown cells are not invented.

## Text precision

Offsets are half-open UTF-8 byte ranges at extended-grapheme boundaries, not
UTF-16 offsets, scalar indexes, glyph indexes, or pixels. Validation/indexing
reuse foundational Unicode. Native strings are preconverted at publication;
client length caps do not split surrogate pairs. Length-aware strings preserve
embedded U+0000; language hints forbid it.

Character movement follows graphemes; Word uses whitespace-delimited units.
Line follows explicit separators; Paragraph uses Line, Page uses Document, and
Format follows annotation boundaries. These explicit fallbacks do not infer
visual layout or rich-editor segmentation.

Typed language, emphasis, spelling, link, and custom annotations are retained;
only representable native attributes
are exported. Scalar attribute queries compare effective values across annotation
boundaries: adjacent equal values remain uniform. The native Link text attribute
is unsupported because a portable annotation node reference does not identify
the destination text range required by UIA; the portable association is retained.

Text ranges provide immutable inspection of submitted content. Native Value,
RangeValue, and Text read-only queries describe the underlying node: explicit
`ReadOnly` takes precedence; otherwise `Editable` or a `SetValue` action indicates
writable content. This does not expose TextEdit or execute editing in Desktop.

`TextFragment` bounds are application-supplied Window-client logical layout.
Rectangle queries return clipped intersecting fragments, not estimated glyph
boxes; degenerate ranges return an empty rectangle array. Point-to-range chooses
the nearest visible fragment's start: publish grapheme-sized fragments for
fine-grained placement. Without
layout, bounding-rectangle results are empty. Multiple text selections require
`SelectionInfo::multiSelectable`.

Selection queries return submitted selections or an explicit caret; neither
requires a mutation action. Without either, the result contains no ranges.
Selection mutation and range-scrolling methods only enqueue typed requests and
require their advertised actions.

Text ranges retain one generation's immutable content/offsets; any newer publication
invalidates them, even if text is unchanged. Clients reacquire ranges. Stable
node providers continue reading the latest tree. Removal/close returns unavailable.

## Actions, notifications, and privacy

Advertise actions and drain owning `ActionRequest` values on the owner thread.
Each accepted request gets a monotonic transport ID and observed generation.
Native success means queued, not executed/committed/acknowledged. Revalidate
against current application state, execute in application code, then publish a
new generation. Already-queued stale requests remain deliverable unchanged.
Only adjacent same-node Focus, SetValue, ScrollTo, and Realize coalesce. Durable
actions preserve FIFO; coalescing cannot cross them. At capacity the newest
non-coalescible request is rejected without losing earlier requests. Selection
payloads distinguish Replace/Add/Remove; scrolling distinguishes logical,
percent (`-1` preserves an axis), and small/large native step units.

Snapshots produce bounded structural, property, focus, selection, text, and
geometry diffs. Pump delivery holds no transport lock across UIA calls.
Publication and actions wake owner waits. Pressure never rolls back a committed
tree: intermediate events, including announcements under actual pressure,
collapse to a latest-generation tree invalidation. Without pressure, refreshing
an invalidation preserves pending announcements. `queueInfo()` reports pending
slots, rejected actions, and collapse counts; clients requery current state.

`announce()` copies a bounded explicit message for later owner pumping.
Acceptance does not guarantee speech. Win32 dynamically detects the native
notification API and reports `Announcements`. It has no per-message locale
override, so `AnnouncementLanguage` is absent and a differing source-node
language returns `Unsupported`; empty/matching hints use source context.

Password role and Password/Sensitive state suppress native value/text content,
related patterns, and announcements. `RedactValue` and `RedactText` target their
respective content; `RedactText` also suppresses announcements. `RedactContent`
hides both, including descriptive strings and announcements.
Delivery rechecks current privacy, preventing older queued
content from being emitted after redaction. This does not sanitize application
storage, portable readers, logs, or inappropriate accessible names; applications
must avoid putting secrets in unrelated semantic fields.

## Limits and integration boundaries

Limits are positive and at most INT32_MAX, extension depth at most 32, and native
provider capacity covers maximum nodes. Defaults bound tree size, cumulative
text, recursive extensions, relation targets, annotations/fragments, actions,
announcements, notifications, activation-lifetime native identities, and
simultaneously retained text ranges. Provider exhaustion fails publication
transactionally. Range exhaustion returns a native quota error until clients
release ranges. Old readers/clients extend snapshot lifetimes, so applications
must bound their own retention and release obsolete readers.

`ChildSurface` and externally owned native descendants stay separate
native/provider surfaces: no automatic grafting, duplicated semantic inference,
or ownership transfer. Input/IME, focus policy, UI layout, Renderer surfaces,
clipboard, shell integration, and commands remain with their established owners.
COM types/native libraries stay private to Desktop and source-tree test fixtures.

## Related pages

- @ref desktop_examples
- @ref desktop_lifecycle_events
- @ref desktop_coordinates_and_dpi
- @ref desktop_testing
- @ref desktop_test_hooks
- @ref desktop_manual_validation
