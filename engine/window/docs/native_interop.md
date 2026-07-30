@page window_native_interop Native Win32 interoperability

Portable consumers include only `window/window.h`. Code that deliberately attaches a renderer or another Win32 integration may opt into:

```cpp
#include "window/native/win32.h"
```

`GameWIP::Window::Native::Win32::getHandle()` returns a non-owning `HINSTANCE`/`HWND` pair. The query returns `NotOpen` for a closed Window. Handles remain valid only until close or a future reopen and must not be destroyed by the consumer.

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

The adapter is installed because renderer surface attachment is a legitimate ABI boundary. Renderer owns the surface and swapchain; Window owns the top-level native handle. After attachment, a renderer can separately opt into `window/integration/renderer_feedback.h` to publish authoritative occlusion transitions on the Window owner thread. See @ref window_renderer_feedback.

Internal backend headers and test hooks are not installed.
