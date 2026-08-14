@page window_native_interop Native Win32 interoperability

Portable consumers include only `window/window.h`. Code that deliberately integrates a renderer or uses another Win32 API may opt into:

```cpp
#include "window/native/win32.h"
```

`GameWIP::Window::Native::Win32::getHandle()` is owner-thread-only and returns a non-owning `HINSTANCE`/`HWND` pair. It returns `ResourceBusy` on another thread and `NotOpen` when no live HWND exists. The handles remain valid only until close or reopen and must not be destroyed by the consumer.

```cpp
GameWIP::Window::Window window;
if (!window.open().ok())
    return;

const auto native = GameWIP::Window::Native::Win32::getHandle(window);
if (!native.status.ok())
    return;

HWND hwnd = native.handle.window;
// Attach a renderer-owned surface without transferring HWND ownership.
```

Including this adapter intentionally includes `windows.h`; isolate it in platform-specific translation units. Do not cache the handle beyond the Window lifetime, replace its window procedure, destroy it, change its owner directly, or mutate state that has a portable Window setter. Such native mutations bypass cached-state and rollback contracts.

The adapter is installed because renderer surface attachment is a legitimate ABI boundary. Renderer owns the surface and swapchain; Window owns the top-level native handle. The renderer bridge and portable display-color queries use `window/renderer_bridge.h`. See @ref window_renderer_integration.

Internal backend headers and test hooks are not installed.

## Related pages

- @ref window_package_abi
- @ref window_lifecycle_and_events
- @ref window_renderer_integration
