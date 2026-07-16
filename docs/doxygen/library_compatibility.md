@page project_library_compatibility Library packaging and compatibility

GameWIP reusable libraries are packaged as CMake config packages with canonical imported targets under the `GameWIP::` CMake namespace. The installed package boundary is the consumer contract for headers, targets, dependencies, version files, and shared-library exports.

## Scope

This page documents installed package names, imported targets, version matching, public-header boundaries, dependency rules, and shared-library export policy.

It does not document individual library APIs. Public APIs are documented in the owning library manual and generated reference pages.

## Packages and targets

| Package | Imported target | Library form |
| --- | --- | --- |
| `IO` | `GameWIP::IO` | Static |
| `FileSystem` | `GameWIP::FileSystem` | Static |
| `Terminal` | `GameWIP::Terminal` | Shared |
| `Logger` | `GameWIP::Logger` | Shared |
| `Assert` | `GameWIP::Assert` | Shared when its runtime is enabled; otherwise interface-only |
| `TestSupport` | `GameWIP::TestSupport` | Static |

The `GameWIP::` prefix belongs to CMake target names. It does not add another level to C++ namespaces.

## Consumer workflow

A clean external CMake project should consume an installed library through `find_package()` and the canonical imported target. The consuming project's dependency lock owns `GAMEWIP_REQUIRED_VERSION`; supply it as a numeric cache or preset value instead of copying a version from this repository:

```cmake
if(NOT DEFINED GAMEWIP_REQUIRED_VERSION OR GAMEWIP_REQUIRED_VERSION STREQUAL "")
    message(FATAL_ERROR "Set GAMEWIP_REQUIRED_VERSION to the required numeric GameWIP package version.")
endif()
find_package(Logger ${GAMEWIP_REQUIRED_VERSION} EXACT CONFIG REQUIRED)
target_link_libraries(MyTarget PRIVATE GameWIP::Logger)
```

Consumers must not use source-tree include paths, build-tree short target names, internal headers, validation headers, or test-hook headers.

## Version and ABI policy

GameWIP packages are pre-1.0 and use exact version matching. Installed config packages request their transitive GameWIP dependencies at the same exact project version.

A consumer must use a compatible compiler, C++ standard library, and runtime ABI. No binary compatibility is promised across package versions, compiler families, standard-library implementations, or MSYS2 environments.

The root `PROJECT_VERSION` is the package version source of truth. Doxygen and runtime diagnostics may display a generated development identity, but package compatibility uses the numeric project version.

See `docs/versioning.md` for the project versioning policy.

## Public-header boundary

Only headers in each target's public CMake file set are installed. Generated shared-library export headers are part of the installed surface when required by a target, but they are visibility scaffolding included by the owning consumer entry header rather than independent entry points. Their installed names and ABI role are documented by the owning library's ABI page.

The following must not be installed or exposed through imported target interfaces:

- Internal headers.
- Platform backend headers.
- Test-hook headers.
- Validation runner headers.
- Benchmark-only headers.
- Source-tree-only helper headers.
- Native platform handles or backend storage layouts unless an explicitly platform-scoped adapter is documented.

Public-header checks compile every supported consumer entry header in isolation and therefore exercise required export scaffolding transitively. The installed-consumer validation checks the complete installed header allowlist and configures outside the source tree using only package config files and installed headers.

## Dependency rules

Use `PUBLIC` CMake dependencies only when a dependency appears in installed public headers or is required by the imported target's public usage requirements. Use `PRIVATE` dependencies for implementation-only usage.

Installed package configs must call `find_dependency()` for public transitive package dependencies. Bundled GameWIP package dependencies should request the same exact project version.

A standalone installed library may still be first-party and project-owned. Standalone means independently consumable from an installed package; it does not mean the library has no repository-level owner or shared version policy.

## Shared-library export policy

Shared-library exports are declaration-driven and checked against reviewed symbol-root allowlists.

`Detail` symbols may be exported only when public templates, macros, or ABI-safe bridges require an out-of-line implementation symbol. They remain implementation support, are hidden from generated API documentation when practical, and carry no independent compatibility guarantee.

Test-hook symbols may exist in validation builds, but their headers are not installed and they are not production API.

## Package validation

Package and compatibility changes must preserve:

- Installed public headers.
- Exported targets.
- Package config files.
- Package version files.
- Public transitive dependencies.
- Reviewed shared-library export allowlists.
- Clean installed-consumer validation.

Run the validation preset after changing package behavior:

```powershell
cmake --preset test
cmake --build --preset test
ctest --preset test
```

Installed-package validation includes the combined surface plus isolated per-package consumers. Each isolated case requests only the package under test, so a missing transitive `find_dependency()` call cannot be masked by an earlier dependency lookup.

## Failure behavior

| Symptom | Likely cause | Action |
| --- | --- | --- |
| `find_package()` cannot find a package. | The library was not installed or `CMAKE_PREFIX_PATH` does not include the install prefix. | Install the project and configure the consumer with the install prefix. |
| A consumer compiles only from the source tree. | Public headers depend on internal paths or build-tree targets. | Move required declarations into the installed public surface or hide the dependency behind implementation code. |
| A package config fails on a dependency. | A public transitive dependency is missing from the config file. | Add the required `find_dependency()` call. |
| Shared export validation fails. | A new exported symbol was not intentionally reviewed. | Update the allowlist only when the symbol belongs to the supported export surface. |

## Maintainer notes

When adding or changing package behavior:

- Update install rules and public file sets.
- Update exported targets and package config files.
- Update exact version behavior only through the root project version policy.
- Add `find_dependency()` for public transitive dependencies.
- Keep internal and test-hook headers out of installed interfaces.
- Re-run public-header checks and clean installed-consumer validation.
- Update this page when the package contract changes.

## Related pages

- @ref project_structure
- @ref project_build
- @ref project_testing
- @ref project_extending
