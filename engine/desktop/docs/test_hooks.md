@page desktop_test_hooks Internal test hooks

Window deterministic failure/state hooks are source-tree-only and are enabled with `DESKTOP_ENABLE_TEST_HOOKS`, which defines
`DESKTOP_INTERNAL_TEST_HOOKS` for repository validation targets.

`desktop/internal/desktop_test_hooks.h` is not installed and is not a supported consumer header. Installed package validation explicitly checks that
`DESKTOP_INTERNAL_TEST_HOOKS` does not leak through `GameWIP::Desktop`.

The hooks cover allocation/native failures, dispatcher setup, title conversion, region/icon/cursor operations, monitor/display/color queries,
fullscreen rollback/restoration, close, event pumping, unexpected native destruction, pointer-hit-mask state, display-color conversion/change
notification, Window and ChildSurface DPI transitions, ChildSurface unexpected native destruction, refresh-rate conversion, and exact exclusive-mode
matching.

Presentation-publication hooks replace the authoritative renderer-facing subset on the owner thread, mirror it only when concurrent reads are
enabled, and expose allocation identity for lazy, idempotent, close/reopen tests. Allocation failure uses the shared one-shot allocation failure
point.

Clipboard hooks provide one-shot failures for allocation, text/path/image preparation, helper owner creation, access, native clear/read/enumeration,
registered-format creation, and close. `failClipboardPublicationAt()` selects a zero-based caller item, while
`failClipboardEnumerationAfter()` preserves a requested materialized prefix before failure. `resetFailures()` clears these thread-local controls.
Hooks preserve the real public cleanup and mutation semantics and never appear in installed headers.

Hook-facing passive types follow the standardized public domains (`Types::Events`, `Types::Display`, `Types::Renderer`) instead of creating a parallel
public vocabulary.
