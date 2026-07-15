@page assert_abi Assert ABI and package boundary

Assert is consumed as the `GameWIP::Assert` CMake target. Its installed public header surface is `debug/assert/assert.h` plus the generated `debug/assert/assert_export.h` export header.

## Package boundary

A clean installed consumer should use:

Set `GAMEWIP_REQUIRED_VERSION` from the consuming project's dependency lock; see @ref project_library_compatibility.

```cmake
find_package(Assert ${GAMEWIP_REQUIRED_VERSION} EXACT CONFIG REQUIRED)
target_link_libraries(MyTarget PRIVATE GameWIP::Assert)
```

The source-tree target name `Assert`, internal headers, platform backend headers, validation hooks, and `AssertCompileInterface` are not installed consumer interfaces.

## Runtime and interface modes

Assert has two package forms:

| Build configuration | Target form | Public header behavior |
| --- | --- | --- |
| Runtime enabled | shared library | Public macros can call exported runtime handlers for failure reporting, popups, debug breaks, and process termination. |
| Runtime disabled | interface-only target | Public macros use header-only disabled behavior. Fatal and recoverable reporting are off. |

The runtime-enabled package has an implementation dependency on Logger. The installed package resolves that dependency through `find_dependency(Logger ... CONFIG)`.

Current compatibility limitation: with CMake 3.23 through 3.29, resolving Logger from a different installation prefix can overwrite `PACKAGE_PREFIX_DIR` before Assert derives its installed Common Controls resource paths. Keep runtime Assert and Logger in the same prefix on those CMake versions, or use CMake 3.30 or newer, until Assert preserves its own prefix across dependency discovery. A custom absolute `CMAKE_INSTALL_DATADIR` is also not supported by the current resource-path concatenation.

## Exported symbols

The reviewed Assert export allowlist contains:

```text
GameWIP::Debug::Assert::debugBreak
GameWIP::Debug::Assert::Detail::handleAssertFailure
GameWIP::Debug::Assert::Detail::handleCheckFailure
GameWIP::Debug::Assert::Detail::handleInteractiveAssertFailure
```

`debugBreak()` is public runtime API. The `Detail::handle*` symbols are exported ABI support for the public macros; consumers must not call them directly or depend on their signatures beyond using the supported macros.

## Binary compatibility

Assert follows the project package policy: packages are pre-1.0, exact-version matched, and not binary-compatible across package versions, compiler families, standard-library implementations, or MSYS2 environments.

Changing exported symbols, public macros, public compile definitions, installed headers, package helpers, or the Logger dependency changes the package boundary and requires documentation and validation updates.

## Related pages

- @ref project_library_compatibility
- @ref assert_public_api
- @ref assert_configuration
- @ref assert_test_hooks
