@page window_native_interop Native interoperability

Portable Window code should use `window/window.h`. Platform-native access is explicit and isolated under `window/native/`.

On Win32, include `window/native/win32.h` and call `GameWIP::Window::Native::Win32::getHandle(window)` or the overload for an open `ChildSurface`.
One `HandleResult` contains one coherent
`HandleView` with both the module instance and HWND required for native interoperability. The API remains singular because the result represents the
Window's native handle view as one value.

`getHandle()` is checked, owner-thread-affine, and returns `NotOpen` for a closed/pending Window. Callers must not destroy, reparent, subclass, or
otherwise mutate ownership behind Window's lifecycle contract unless a specific interop contract says that behavior is supported.

Native handles are borrowed views. Their validity ends with native destruction/close and they must not be cached across lifetime transitions.

The ChildSurface HWND is a GameWIP-owned host. External technology may create native descendants below it, but must not destroy, reparent, subclass,
or overwrite GameWIP state on the host. See @ref window_child_surfaces.
