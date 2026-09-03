@page desktop_test_hooks Internal test hooks

Window deterministic failure/state hooks are source-tree-only and are enabled with `DESKTOP_ENABLE_TEST_HOOKS`, which defines
`DESKTOP_INTERNAL_TEST_HOOKS` for repository validation targets.

`desktop/internal/desktop_test_hooks.h` is not installed and is not a supported consumer header. Installed package validation explicitly checks that
`DESKTOP_INTERNAL_TEST_HOOKS` does not leak through `GameWIP::Desktop`.

The hooks cover allocation/native failures, dispatcher setup, title conversion, region/icon/cursor operations, monitor/display/color queries,
fullscreen rollback/restoration, close, event pumping, unexpected native destruction, pointer-hit-mask state, display-color conversion/change
notification, Window and ChildSurface DPI transitions, ChildSurface unexpected native destruction, refresh-rate conversion, and exact exclusive-mode
matching.

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

Hook-facing passive types follow the standardized public domains (`Types::Events`, `Types::Display`, `Types::Renderer`) instead of creating a parallel
public vocabulary.
