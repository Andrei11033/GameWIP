@page project_cmake_infrastructure CMake infrastructure

GameWIP CMake infrastructure is the maintainer-facing build layer used to compose first-party libraries, validation executables, documentation, reports, packages, and platform backends. It is documented for contributors who extend the project, not for players.

The sections below explain which shared helpers exist, what contract each one
provides, and when a local CMake file should use or extend them. Private local
variables and incidental implementation branches stay in the source instead of
being repeated here.

## Scope

Consult this page when adding or changing:

- A CMake preset or project option.
- A reusable library target.
- A validation-test module.
- A benchmark module.
- A Doxygen manual page or library documentation registration.
- A platform backend.
- Runtime dependency copying.
- Coverage, static-analysis, sanitizer, or documentation targets.
- Install, package, or exported-target behavior.

Use @ref project_build for normal configure and build commands. Use @ref project_extending for the full add/change checklist.

## File ownership

| File | Owns |
| --- | --- |
| `CMakeLists.txt` | Root project composition and top-level target creation. |
| `CMakePresets.json` | Supported configure, build, and test presets. |
| `cmake/GameWIPOptions.cmake` | Project composition options and option compatibility checks. |
| `cmake/GameWIPPlatform.cmake` | Normalized platform resolution and required backend-file inclusion. |
| `cmake/GameWIPPackage.cmake` | Repeated package config, version, and export installation ceremony. |
| `cmake/GameWIPWarnings.cmake` | First-party C++ warning policy, including opt-in warnings as errors. |
| `cmake/LibraryDoxygen.cmake` | Doxygen input registration and generated documentation target creation. |
| `cmake/GameWIPDocumentation.cmake` | Project-level Doxygen page registration. |
| `cmake/GameWIPValidationModules.cmake` | Validation and benchmark module registration helpers. |
| `cmake/GameWIPRuntimeDependencies.cmake` | Runtime dependency staging and validation registration for executable targets. |
| `cmake/copy_runtime_dependencies.cmake` | Clean-shadow runtime dependency discovery and app-local DLL staging. |
| `cmake/ValidateRuntimeDependencies.cmake` | Windows/MSYS2 regression contract for compiler-runtime replacement and executable launch. |
| `cmake/GameWIPCoverage.cmake` | Coverage instrumentation and report target. |
| `cmake/GameWIPStaticAnalysis.cmake` | clang-tidy and clang-format validation targets. |
| `cmake/GameWIPSanitizers.cmake` | AddressSanitizer availability and flags. |
| `cmake/GameWIPVersion.cmake` | Project version detection, display-version composition, and generated version header. |
| `cmake/RunInstalledConsumerValidation.cmake` | Clean installed-consumer package validation. |
| `cmake/ValidateExportedSymbols.cmake` | Shared-library exported-symbol allowlist validation. |
| `cmake/export_allowlists/` | Expected public exported-symbol roots for shared-library checks. |

Library-local CMake helpers belong under the owning library's `cmake/` directory. Do not put library-specific policy in the root `cmake/` folder unless it is genuinely shared by multiple libraries.

## Presets and project options

Supported developer workflows start from `CMakePresets.json`. Presets set intentional combinations of options from `cmake/GameWIPOptions.cmake`.

Project options use the `GAMEWIP_` prefix. They control repository composition, instrumentation, validation, documentation, and tool workflows. Library-local options use the owning library prefix when they are meaningful outside the root project.

When adding or changing an option:

- Define it in the owning CMake file.
- Set intentional values in each relevant preset.
- Document project-facing behavior in @ref project_build.
- Document library-local consumer behavior in the owning library manual.
- Add validation coverage for meaningful enabled and disabled behavior.

Do not add hidden option coupling. If one option requires another, express that relationship with an explicit CMake check and a clear error message.

## Library targets

Reusable libraries own their own `CMakeLists.txt` files. A library CMake file must:

- Create one canonical target.
- Provide one canonical project alias when appropriate.
- Require `cxx_std_23`.
- List sources explicitly.
- Declare public include directories through target properties.
- Use `PUBLIC` dependencies only when the dependency appears in installed public headers.
- Use `PRIVATE` dependencies for implementation-only requirements.
- Install only public headers and generated export headers.
- Register package config and exact version files for installable libraries.
- Register public headers and docs with `gamewip_register_doxygen_library()`.

Avoid global include directories, global compile definitions, and recursive source discovery for maintained library sources.

TestSupport remains a validation-oriented leaf relative to higher-level reusable libraries. It may link foundational Unicode when actual UTF-8 text semantics require it, but it must not acquire IO, FileSystem, Terminal, Window, Logger, Assert, engine, or other higher-level GameWIP dependencies for convenience. `TEST_SUPPORT_ENABLE_TEST_HOOKS` may enable deterministic failure injection only for source-tree validation composition; neither the option's internal compile definition nor its hook header belongs to the installed target.

## Documentation helpers

Use `gamewip_register_doxygen_inputs()` for project-level Doxygen pages and other explicit Doxygen inputs.

Use `gamewip_register_doxygen_library()` from a library `CMakeLists.txt` to register the library landing page, manual pages, and public headers:

```cmake
gamewip_register_doxygen_library(
    NAME Logger
    PAGE_ID logger
    PUBLIC_HEADERS
        "${CMAKE_CURRENT_SOURCE_DIR}/logger.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/logger_macros.h"
    DOCS
        "${CMAKE_CURRENT_SOURCE_DIR}/docs"
)
```

Use `gamewip_create_documentation_target()` only from the root project after all project and library Doxygen inputs have been registered.

Doxygen inputs are explicit by design. Do not enable recursive source-tree discovery to make missing registrations disappear.

## Validation modules

Use `gamewip_add_test_module()` for correctness-test modules under `game/validation/tests/<module>/`.

```cmake
gamewip_add_test_module(
    NAME filesystem
    SOURCES
        module.cpp
        filesystem_test.cpp
    LINK_LIBRARIES
        FileSystem
        TestSupport
)
```

The CMake helper registers sources and dependencies. Runtime module name, order, and adapter behavior remain in the module's `module.cpp` registration.

Use `gamewip_add_validation_module_directories()` only from validation parent directories that discover module subdirectories.

## Benchmark modules

Use `gamewip_add_benchmark_module()` for Google Benchmark modules under `game/validation/benchmarks/<module>/`.

```cmake
gamewip_add_benchmark_module(
    NAME logger
    SOURCES
        logger_benchmark.cpp
    LINK_LIBRARIES
        Logger
)
```

Benchmark modules must remain separate from correctness tests. CMake registration proves that a benchmark is available; benchmark pages explain how to collect meaningful measurements.

## Platform backends

Use `gamewip_resolve_platform_id()` once at root configuration time to determine the project platform ID.

Use `gamewip_target_platform_backend(TARGET <target> ROOT <platform-root>)` from every platform-aware target. The helper includes exactly the active backend's required `platform.cmake`; that file owns backend sources, system libraries, resources, private includes, and compile definitions.

The normal backend shape is:

```text
<library>/platform/<platform-id>/
  platform.cmake
  <platform-id>_<feature>.cpp
```

Platform backend behavior is governed by @ref project_platform_backend_contract.

## Runtime dependencies

Use `gamewip_copy_runtime_dependencies(<target>)` for executable targets that must run directly from the build tree on the supported Windows/MSYS2 environment.

The helper adds a post-build step that resolves runtime DLLs and copies them beside the target executable. It exists so validation executables and the game executable can run without requiring users to manually copy compiler runtime dependencies.

On Windows, discovery scans a clean shadow copy so a stale compiler DLL already
beside the executable cannot override the active toolchain runtime. Project DLLs
remain available to the scan, compiler-owned DLLs are resolved from the active
compiler directory, and the temporary shadow directory is removed afterward.
Unresolved dependencies are warnings by default and become errors when the
caller sets `GAMEWIP_FAIL_ON_UNRESOLVED_DEPENDENCIES`.

Use `gamewip_add_runtime_dependencies_validation(<target>)` to register the
`validation.cmake.runtime_dependencies` CTest contract for the supported
Windows/MSYS2 GNU configuration. The test stages an intentionally stale
`libstdc++-6.dll`, reruns dependency copying with strict unresolved-dependency
handling, verifies that the active compiler runtime replaced it, and launches a
focused runner module from the staged directory.

## Coverage, static analysis, and sanitizers

Use the project-level targets created by:

- `gamewip_create_coverage_target()`
- `gamewip_create_static_analysis_targets()`
- `cmake/GameWIPSanitizers.cmake`

These helpers are controlled by presets and `GAMEWIP_` options. Do not enable coverage, static analysis, or sanitizer behavior from individual library CMake files unless a library-local option has an explicitly documented reason.

## Version and package validation

`gamewip_configure_version()` generates the build identity used by runtime diagnostics and documentation. Version policy is documented in @ref project_versioning.

Package-boundary validation is part of the validation workflow. Changes to install rules, exported targets, package config files, exact version files, or public dependency visibility must preserve clean installed-consumer validation. Combined and isolated consumers must also reject every source-tree-only test-hook definition, including TestSupport's.

Shared-library exported-symbol checks use allowlists under `cmake/export_allowlists/`. Update an allowlist only when the public exported surface intentionally changes.

## Maintainer rules

- Prefer target-local properties over global CMake state.
- Register maintained sources explicitly.
- Keep project composition options in root project infrastructure.
- Keep library-local behavior in the owning library.
- Keep platform selection behind `GameWIPPlatform.cmake` and all target wiring in backend `platform.cmake` files.
- Keep Doxygen inputs explicit.
- Keep validation and benchmark registration through the project helpers.
- Keep installed public headers free of internal and test-hook paths.
- Document every new maintainer-facing helper before using it widely.

## Add or change checklist

Before merging a CMake infrastructure change:

- Confirm the owner file is correct.
- Confirm all affected presets still configure.
- Confirm target dependencies use the narrowest correct visibility.
- Confirm installed public headers and packages still validate when package behavior changes.
- Confirm Doxygen inputs are registered explicitly when docs change.
- Confirm validation and benchmark modules still register through the helper layer.
- Confirm backend changes preserve the platform backend contract.
- Record the exact configure, build, test, or docs commands in the pull request.

## Related pages

- @ref project_build
- @ref project_extending
- @ref project_documentation
- @ref project_platform_backend_contract
- @ref project_library_compatibility
