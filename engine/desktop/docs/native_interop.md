@page desktop_native_interop Native interoperability

Portable code uses `desktop/window.h`; platform-native access is explicit under `desktop/native/`.

## Win32 handles

On Win32, include `desktop/native/win32.h` and call `GameWIP::Desktop::Native::Win32::getHandle(window)` (or the open-`ChildSurface` overload).
The `Types::Result<HandleView>` holds one coherent view with both module instance and HWND.

`getHandle()` is checked and owner-thread-affine, and returns `NotOpen` for a closed/pending Window. Do not destroy, reparent, subclass, or
otherwise mutate ownership unless an interop contract allows it.

Handles are borrowed views, valid only until native destruction/close; do not cache them across lifetime transitions.

The ChildSurface HWND is a GameWIP-owned host: external technology may create descendants below it but must not destroy, reparent, subclass,
or overwrite its state. See @ref desktop_child_surfaces.

## Message sinks

`attachMessageSink()` registers an external observer to receive owner-thread Win32 messages before Desktop's internal routing. The sink callback
receives the window, message, wParam, and lParam; it does not replace Desktop's window procedure or override the returned native result.

The sink is invoked synchronously during Desktop's message pump for each message before internal routing. Blocking operations in the sink
block the owner thread and delay all message delivery; keep implementations lightweight. Only one sink per window is supported;
`attachMessageSink()` returns `AlreadyOpen` if one is already registered.

Attach and detach only from the owner thread. See @ref GameWIP::Input::Platform::Win32::attachToWindow for standard integration with the Input library.
