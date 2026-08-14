@page window_package_abi Package and ABI boundary

## Installed package

Installed consumers use the exact-version `Window` config package and link `GameWIP::Window`:

```cmake
find_package(Window ${GAMEWIP_REQUIRED_VERSION} EXACT CONFIG REQUIRED)
target_link_libraries(MyTarget PRIVATE GameWIP::Window)
```

The package resolves its public IO and FileSystem dependencies. The generated `window/window_export.h` file supplies shared-library visibility and is exercised through supported entry headers rather than documented as an independent API.

## Public headers

- `window/window.h` is the canonical portable entry point.
- `window/renderer_bridge.h` is the optional portable renderer bridge and display-color query entry point.
- `window/native/win32.h` is installed and documented only for Win32 packages and deliberately includes `windows.h`.

Internal backend headers, `window/internal/window_test_hooks.h`, source-tree compile definitions, and platform implementation files are not installed consumer interfaces.

## Host DPI requirement

On Windows, linking `GameWIP::Window` propagates Window's application resource to executable consumers in both build-tree and installed-package use. The manifest declares Common Controls v6, Per-Monitor-V2 `dpiAwareness`, and the compatible legacy `dpiAware` setting.

Window therefore does not depend on Assert for DPI policy. `open()` validates the effective thread context and returns `Unsupported` when the host is incompatible; it never changes process DPI awareness at runtime.

A minimal manifest fragment is:

```xml
<application xmlns="urn:schemas-microsoft-com:asm.v3">
  <windowsSettings>
    <dpiAwareness xmlns="http://schemas.microsoft.com/SMI/2016/WindowsSettings">PerMonitorV2</dpiAwareness>
  </windowsSettings>
</application>
```

The package compiles this as the executable's single `CREATEPROCESS_MANIFEST_RESOURCE_ID RT_MANIFEST` resource. Assert's standalone Common Controls helper is explicit, so ordinary Window + Assert linking does not add a competing resource.

## Compatibility

`Window` is a shared C++23 library with a pImpl object boundary. Consumers follow the compiler, standard-library, runtime, architecture, and exact-version policy in @ref project_library_compatibility. The reviewed shared-library symbol roots are maintained in `cmake/export_allowlists/window.txt`.

## Related pages

- @ref window_quick_start
- @ref window_native_interop
- @ref window_testing
