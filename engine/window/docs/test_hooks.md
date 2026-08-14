@page window_test_hooks Internal test hooks

Window deterministic failure/state hooks are source-tree-only and are enabled with `WINDOW_ENABLE_TEST_HOOKS`, which defines `WINDOW_INTERNAL_TEST_HOOKS` for repository validation targets.

`window/internal/window_test_hooks.h` is not installed and is not a supported consumer header. Installed package validation explicitly checks that `WINDOW_INTERNAL_TEST_HOOKS` does not leak through `GameWIP::Window`.

The hooks cover allocation/native failures, dispatcher setup, title conversion, region/icon/cursor operations, monitor/display/color queries, fullscreen rollback/restoration, close, event pumping, unexpected native destruction, pointer-hit-mask state, display-color conversion/change notification, DPI transitions, refresh-rate conversion, and exact exclusive-mode matching.

Hook-facing passive types follow the standardized public domains (`Types::Events`, `Types::Display`, `Types::Renderer`) instead of creating a parallel public vocabulary.
