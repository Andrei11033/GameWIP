@page window_package_abi Package and ABI contract

Window is intentionally built as a **shared C++23 library**. The Win32 implementation owns process-local Window IDs, the native-window registry,
class-registration state, and per-thread dispatcher registry. A shared module gives those facilities one coherent runtime instance across
executable/DLL boundaries. Static Window linkage is not advertised; supporting it would first require a runtime design that cannot duplicate process
identity/state per consuming module.

The exported `Window` object keeps a pImpl boundary. Public C++ types remain part of the exact-version package contract and are not a stable
cross-version C ABI.

## Installed headers

The supported public headers are:

- `window/types.h`
- `window/description.h`
- `window/events.h`
- `window/display.h`
- `window/display_info.h`
- `window/cursor.h`
- `window/window.h`
- `window/renderer_bridge.h`
- `window/native/win32.h` on Win32
- generated `window/window_export.h`

Internal headers and test hooks are source-tree-only. Every supported entry header is compiled in isolation by repository validation.

`window/window.h` intentionally includes the normal shared vocabulary, description, fundamental display-mode surface, and events. Rich display
inspection, custom cursor resources, renderer integration, and native interop remain explicit opt-in includes.

## Dependencies

`IO` and `FileSystem` are public package dependencies because installed public types expose their contracts. Unicode is a private implementation
dependency used by the shared Window library for strict native text conversion; no Unicode type leaks through Window public headers.

The package remains exact-version matched. On Win32 the installed package also propagates the Window application manifest resource required for
Per-Monitor-V2 awareness.

## Internal definitions

Source-tree validation may enable `WINDOW_INTERNAL_TEST_HOOKS`. Installed imported targets must not expose that definition. Window-specific build
variables likewise use the `WINDOW_INTERNAL_*` prefix rather than project-wide `GAMEWIP_*` names.
