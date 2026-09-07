@page desktop_package_abi Package and ABI contract

Window is intentionally built as a **shared C++23 library**. The Win32 implementation owns process-local Window IDs, the native-window registry,
class-registration state, and per-thread dispatcher registry. A shared module gives those facilities one coherent runtime instance across
executable/DLL boundaries. Static Window linkage is not advertised; supporting it would first require a runtime design that cannot duplicate process
identity/state per consuming module.

The exported `Window` object keeps a pImpl boundary. Public C++ types remain part of the exact-version package contract and are not a stable
cross-version C ABI.

`DragDropTarget` uses the same exported pImpl resource boundary. The passive
`Types::DragDrop` aggregates and variants are exact-version C++ package types;
changing their layout, alternatives, enum representation, or field types is an
ABI change. `DragDrop::beginDrag()` is an exported free operation in the same
shared library.

## Installed headers

The supported public headers are:

- `desktop/types.h`
- `desktop/description.h`
- `desktop/events.h`
- `desktop/display.h`
- `desktop/display_info.h`
- `desktop/cursor.h`
- `desktop/child_surface.h`
- `desktop/data_transfer.h`
- `desktop/drag_drop.h`
- `desktop/clipboard.h`
- `desktop/window.h`
- `desktop/renderer_bridge.h`
- `desktop/native/win32.h` on Win32
- generated `desktop/desktop_export.h`

Internal headers and test hooks are source-tree-only. Every supported entry header is compiled in isolation by repository validation.

`desktop/window.h` intentionally includes the normal shared vocabulary, description, fundamental display-mode surface, and events. Rich display
inspection, custom cursor resources, native child hosts, Clipboard/data transfer, native drag and drop, renderer integration, and native interop remain
explicit opt-in includes.

## Dependencies

`IO` and `FileSystem` are public package dependencies because installed public types expose their contracts. Unicode is a private implementation
dependency used by the shared Desktop library for strict native text conversion; no Unicode type leaks through Desktop public headers.

The package remains exact-version matched. On Win32 the installed package also propagates the Window application manifest resource required for
Per-Monitor-V2 awareness.

OLE and COM remain private implementation dependencies. The Win32 backend links
`ole32`; public and installed headers expose no COM interfaces, HRESULT values,
or raw drag/drop handles.

## Internal definitions

Source-tree validation may enable `DESKTOP_INTERNAL_TEST_HOOKS`. Installed imported targets must not expose that definition. Desktop-specific build
variables likewise use the `WINDOW_INTERNAL_*` prefix rather than project-wide `GAMEWIP_*` names.
