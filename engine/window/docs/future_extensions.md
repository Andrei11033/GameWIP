@page window_future_extensions Future extension areas

The following areas are intentionally documented without placeholder fields, unused runtime memory, speculative virtual interfaces, or new dependencies.

## ChildSurface

A future separate library may host native child surfaces for browser embedding, third-party SDKs, native controls, or specialized rendering surfaces. Current Window models only native top-level windows.

## Platform shell integration

Taskbar or dock features, notifications, tray icons, jump lists, file associations, recent files, and application-shell policy belong in a future shell integration add-on.

## Accessibility provider bridge

A future UI system will own the semantic accessibility tree. Window may host a platform bridge when that owner and contract exist; it does not invent an accessibility model now.

## Renderer-driven alpha-mask hit testing

Window currently supports whole-client and rectangular-region pointer pass-through. A future Renderer integration may provide an alpha-mask source while retaining the declarative native boundary.

## Resource and dialog additions

Custom cursor image resource objects, clipboard services, and native dialogs remain possible focused additions. They are not represented by incomplete handles or placeholder APIs in the current Window contract.

Renderer surface attachment already has an explicit non-owning native-handle boundary, and renderer occlusion reporting has an explicit owner-thread feedback boundary. Renderer surface lifetime, swapchains, and synchronization remain Renderer responsibilities.

Input event bridging still needs an explicit contract when Input is redesigned. Window does not freeze that owning library's policy in advance.
