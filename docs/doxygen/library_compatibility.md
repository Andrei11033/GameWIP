@page library_compatibility Library packaging and compatibility

Reusable libraries install CMake config packages and expose canonical imported targets under the `GameWIP::` CMake namespace:

| Package | Imported target | Library form |
| --- | --- | --- |
| `IO` | `GameWIP::IO` | Static |
| `FileSystem` | `GameWIP::FileSystem` | Static |
| `Terminal` | `GameWIP::Terminal` | Shared |
| `Logger` | `GameWIP::Logger` | Shared |
| `Assert` | `GameWIP::Assert` | Shared when its runtime is enabled; otherwise interface-only |
| `TestSupport` | `GameWIP::TestSupport` | Static |

The `GameWIP::` prefix belongs to CMake target names. It does not add another level to the C++ namespaces.

Packages are pre-1.0 and use exact version matching. A consumer must use a compatible compiler, C++ standard library, and runtime ABI. Installed config packages request their transitive GameWIP dependencies at the same exact version. No binary compatibility is promised across package versions or toolchains.

Example:

```cmake
find_package(Logger 0.0.1 EXACT CONFIG REQUIRED)
target_link_libraries(MyTarget PRIVATE GameWIP::Logger)
```

Only headers in each target's public CMake file set are installed. Generated export headers are part of that surface; internal headers and test hooks are not. Public-header checks compile every installed entry header in isolation, and an installed-consumer validation configures outside the source tree using only package configs and installed headers.

Shared-library exports are declaration-driven and checked against reviewed symbol-root allowlists. `Detail` symbols are exported only when public templates or macros need an out-of-line bridge. They remain implementation support, are hidden from generated API documentation, and carry no independent compatibility guarantee. Test-hook symbols may exist in validation builds, but their headers are not installed and they are not production API.
