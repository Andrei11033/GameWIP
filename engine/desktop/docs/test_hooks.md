@page desktop_test_hooks Internal test hooks

Desktop deterministic failure/state hooks are source-tree-only and are enabled with `DESKTOP_ENABLE_TEST_HOOKS`, which defines
`DESKTOP_INTERNAL_TEST_HOOKS` for repository validation targets.

`desktop/internal/desktop_test_hooks.h` is not installed and is not a supported consumer header. Installed package validation explicitly checks that
`DESKTOP_INTERNAL_TEST_HOOKS` does not leak through `GameWIP::Desktop`.

The hooks cover allocation/native failures, dispatcher setup, title conversion, region/icon/cursor operations, monitor/display/color queries,
fullscreen rollback/restoration, close, event pumping, unexpected native destruction, pointer-hit-mask state, display-color conversion/change
notification, Window and ChildSurface DPI transitions, ChildSurface unexpected native destruction, refresh-rate conversion, and exact exclusive-mode
matching.

Shell hooks add three one-shot native-boundary failures: `ShellNativeOpen`, `ShellNativeApply`,
and `ShellNativeClose`. They make resource-open failure, transactional publication failure, and
retryable close failure deterministic without replacing the Win32 shell objects. The
`enqueueShellEvent()` hook injects a typed payload through the real fixed-capacity queue path so
FIFO order, sequence reset, overflow, and owned-event lifetime can be tested without requiring a
human to activate a tray, taskbar, or notification surface.

DragDrop hooks expose portable effect negotiation, source completion mapping,
and deterministic target-event injection for queue/coalescing/terminal-event
tests. They do not expose COM objects and remain source-tree-only. One-shot
DragDrop failure points cover OLE initialization for target and source paths,
target registration, revocation, source preparation, and final materialization.
A consecutive-revocation control also validates retryable close, whole-chain
deferred cleanup, Window-destruction finalization, and process-isolated
dispatcher exit. Passive active/deferred target counts prove that multi-target
cleanup retains every state without exposing COM objects. The COM contract
fixture checks source enumeration/data-query rules without exposing a native
interface to tests.

Presentation-publication hooks replace the authoritative renderer-facing subset on the owner thread, mirror it only when concurrent reads are
enabled, and expose allocation identity for lazy, idempotent, close/reopen tests. Allocation failure uses the shared one-shot allocation failure
point.

Clipboard hooks provide one-shot failures for allocation, text/path/image preparation, helper owner creation, access, native clear/read/enumeration,
registered-format creation, and close. `failClipboardPublicationAt()` selects a zero-based caller item, while
`failClipboardEnumerationAfter()` preserves a requested materialized prefix before failure. `resetFailures()` clears these thread-local controls.
Hooks preserve the real public cleanup and mutation semantics and never appear in installed headers.

Dialog hooks arm one deterministic completion for each file/folder, Message, or Prompt operation. Their snapshots expose the converted native text,
ordered filter names and wildcard patterns, backend button/option IDs, defaults, flags, and secondary content without opening interactive UI. Failure
points cover text conversion, suggested-directory setup, modal invocation, result materialization, ProgressDialog class registration/release, owner
blocking, owner-restore wake delivery, and native mutation. The owner-restore wake seam makes the dispatcher retry path deterministic without
replacing native ownership. ProgressDialog inspection keeps the real HWND and controls authoritative while exposing passive bounds, text, range,
position, marquee, cancellation, owner, registry, deferred-cleanup, and class-reference state. Test-only actions request cancel/close, simulate a DPI
suggested rectangle, and destroy the native window; they do not replace the public lifecycle path.

The passive `progressOwnerRestoreMessageRegistrationAttempted()` observation
supports a fresh-process regression that ordinary Window use does not register
the ProgressDialog owner-restore message.

Accessibility hooks use the real snapshot-backed implementation:
`submitAccessibilityAction()` enters the same bounded validated transport used
by native providers; `popAccessibilityNotification()` inspects its pending diff
descriptors without requiring a listening assistive client. Win32-only
`accessibilityProvider()` returns an AddRef'ed raw provider pointer for COM
contract checks; tests release it through `IRawElementProviderSimple::Release()`.
This opaque test seam is not a portable public API or installed native escape
hatch. Generic allocation failure exercises transactional publication, and
presentation hooks exercise cached geometry without changing semantic content.
The real UIA client child protocol does not require any accessibility hook.

`WindowStyleQuery` is a one-shot native style/ex-style query failure used by
checked geometry, mode, control, DPI, child-surface, and ProgressDialog paths.
`WindowUserDataInstallation` is a one-shot top-level `WM_NCCREATE` userdata
installation failure. `WindowCreationCallback` returns `-1` from `WM_CREATE`
after `WM_NCCREATE` has installed userdata, so it exercises failed native
construction cleanup. These return deterministic native failure status and are
cleared by `resetFailures()`; neither can publish the new Window state.

Hook-facing passive types follow the standardized public domains (`Types::Events`, `Types::Display`, `Types::Renderer`) instead of creating a parallel
public vocabulary.
