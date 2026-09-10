@page assert_abi ABI and package boundary

Assert is consumed as the `GameWIP::Assert` CMake target. Its installed public header surface is `debug/assert/assert.h` plus the generated
`debug/assert/assert_export.h` export header.

## Package boundary

A clean installed consumer should use:

Set `GAMEWIP_REQUIRED_VERSION` from the consuming project's dependency lock; see @ref project_library_compatibility.

```cmake
find_package(Assert ${GAMEWIP_REQUIRED_VERSION} EXACT CONFIG REQUIRED)
target_link_libraries(MyTarget PRIVATE GameWIP::Assert)
```

The source-tree target name `Assert`, internal headers, platform backend headers, and validation hooks are not installed consumer interfaces.

## Runtime and interface modes

Assert has two package forms:

| Build configuration | Target form | Public header behavior |
| --- | --- | --- |
| Runtime enabled | shared library | Public macros can call exported runtime handlers for failure reporting, popups, debug breaks, and process termination. |
| Runtime disabled | interface-only target | Public macros use header-only disabled behavior. Fatal and recoverable reporting are off. |

The runtime uses Logger for reporting and links Unicode statically for popup text conversion. Unicode is a private implementation dependency, and
Assert's public headers do not expose Unicode types. The installed runtime package resolves the matching Logger package through
`find_dependency(Logger ... CONFIG)` so the required shared runtime is available to consumers.

Both package forms resolve the matching `GameWIPApplication` package, which supplies the shared application manifest helper. Assert does not install
manifest resources or attach them to consumers. Applications select their process requirements explicitly; see @ref assert_configuration.

## Exported symbols

The reviewed Assert export allowlist contains:

```text
GameWIP::Debug::Assert::debugBreak
GameWIP::Debug::Assert::Detail::handleAssertFailure
GameWIP::Debug::Assert::Detail::handleCheckFailure
GameWIP::Debug::Assert::Detail::handleInteractiveAssertFailure
```

`debugBreak()` is public runtime API. The `Detail::handle*` symbols are exported ABI support for the public macros; consumers must not call them
directly or depend on their signatures beyond using the supported macros.

## Binary compatibility

Assert follows the project package policy: packages are pre-1.0, exact-version matched, and not binary-compatible across package versions, compiler
families, standard-library implementations, or MSYS2 environments.

Changing exported symbols, public macros, public compile definitions, installed headers, package helpers, or private runtime dependencies changes the
package boundary and requires documentation and validation updates.

## Related pages

- @ref project_library_compatibility
- @ref assert_public_api
- @ref assert_configuration
- @ref assert_test_hooks
