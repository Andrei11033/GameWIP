@page desktop_package_abi Package and ABI contract

Desktop is a **shared C++23 library**. The Win32 implementation owns process-local Window IDs, the native-window registry,
class-registration state, and the per-thread dispatcher registry; a shared module gives these one runtime instance across
executable/DLL boundaries. Static linkage is not advertised: it would need a runtime design that cannot duplicate process
state per consuming module.

All public C++ types are exact-version package contract, not a stable cross-version C ABI. `Window` and `DragDropTarget` are exported pImpl resources;
changing layout, alternatives, enum representation, or field types of passive `Types::DragDrop` aggregates and variants is an ABI change.
`DragDrop::beginDrag()` is an exported free operation. Shell resources, values, and services and the accessibility values, builders, readers, and
non-owning Window façade follow the same contract; native providers and COM types stay private.

## Installed headers

The supported public headers are:

- `desktop/types.h`
- `desktop/description.h`
- `desktop/events.h`
- `desktop/display.h`
- `desktop/display_info.h`
- `desktop/cursor.h`
- `desktop/child_surface.h`
- `desktop/accessibility.h`
- `desktop/data_transfer.h`
- `desktop/drag_drop.h`
- `desktop/clipboard.h`
- `desktop/window.h`
- `desktop/dialogs.h`
- `desktop/renderer_bridge.h`
- `desktop/result.h`
- `desktop/shell.h`
- `desktop/shell_types.h`
- `desktop/shell_taskbar.h`
- `desktop/shell_tray.h`
- `desktop/shell_notifications.h`
- `desktop/shell_jump_lists.h`
- `desktop/shell_registration.h`
- `desktop/native/win32.h` on Win32
- generated `desktop/desktop_export.h`

Internal headers and test hooks are source-tree-only. Every supported entry header is compiled in isolation by repository validation.

`DESKTOP_EXPORT` marks installed production/runtime ABI. `DESKTOP_TEST_EXPORT`
is limited to source-tree validation hooks and is never installed.

`desktop/window.h` includes the shared vocabulary, description, display-mode surface, and events. Rich display
inspection, cursors, child hosts, accessibility, Clipboard/data transfer, drag and drop, dialogs, shell, renderer
integration, and native interop are explicit opt-in includes.

## Dependencies

`IO` and `FileSystem` are public package dependencies because public types expose their contracts. Unicode is a private
dependency for strict native text conversion; no Unicode type appears in public headers.

The package is exact-version matched. Applications must call
`gamewip_attach_application_manifest(TARGET <executable> PER_MONITOR_V2)` from the
shared `GameWIPApplication` package; Desktop never propagates an executable
resource through its library target. `find_package(Desktop)` loads the helper through its exact-version
`GameWIPApplication` dependency. Attach the manifest to the final executable,
including when a separate library links Desktop. See @ref desktop_quick_start
and @ref project_cmake_infrastructure.

Native Win32 dependencies are private: the backend
links `ole32`, `oleaut32` (Automation values), `uiautomationcore` (accessibility providers), and `comctl32`;
none is a consumer link requirement. Linking `comctl32` does not activate Common Controls v6; that is executable
manifest policy owned by `GameWIPApplication`.
Public headers expose no COM interfaces, HRESULT values, or raw
drag/drop or accessibility-provider handles.

## Internal definitions

Source-tree validation may enable `DESKTOP_INTERNAL_TEST_HOOKS`. Installed imported targets must not expose that definition. Desktop-specific internal
build variables and macros use the `DESKTOP_INTERNAL_*` prefix rather than project-wide `GAMEWIP_*` names.
