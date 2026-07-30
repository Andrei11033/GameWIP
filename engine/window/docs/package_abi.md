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
- `window/renderer.h` is the optional portable renderer-integration entry point.
- `window/native/win32.h` is installed and documented only for Win32 packages and deliberately includes `windows.h`.

Internal backend headers, `window/internal/window_test_hooks.h`, source-tree compile definitions, and platform implementation files are not installed consumer interfaces.

## Host DPI requirement

On Windows, the host executable must establish Per-Monitor-V2 DPI awareness through its application manifest. Window validates the effective thread context during `open()` and returns `Unsupported` with a diagnostic when the host is incompatible. The library does not call `SetProcessDpiAwarenessContext()` or silently change process-wide policy.

A minimal manifest fragment is:

```xml
<application xmlns="urn:schemas-microsoft-com:asm.v3">
  <windowsSettings>
    <dpiAwareness xmlns="http://schemas.microsoft.com/SMI/2016/WindowsSettings">PerMonitorV2</dpiAwareness>
  </windowsSettings>
</application>
```

Embed it as `CREATEPROCESS_MANIFEST_RESOURCE_ID RT_MANIFEST` in a Win32 resource compiled into the executable. GameWIP executables receive this setting through the project manifest resource.

## Compatibility

`Window` is a shared C++23 library with a pImpl object boundary. Consumers still follow the project compiler, standard-library, runtime, architecture, and exact-version policy described by @ref project_library_compatibility. The reviewed shared-library symbol roots are maintained in `cmake/export_allowlists/window.txt`.

## Related pages

- @ref window_quick_start
- @ref window_native_interop
- @ref window_testing
