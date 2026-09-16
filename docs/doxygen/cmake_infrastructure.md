@page project_cmake_infrastructure CMake infrastructure

GameWIP's CMake infrastructure is the maintainer-facing build layer for
first-party libraries, validation executables, documentation, reports,
packages, and platform backends. This page is for contributors who extend the
project.

The sections below describe the shared helpers and the contracts they provide.
Private variables and incidental implementation details belong in the source,
not in this overview.

## Scope

This page covers CMake presets and options, reusable library targets, validation
and benchmark modules, Doxygen registration, platform backends, runtime
dependency copying, coverage and analysis targets, and install or package
behavior.

Use @ref project_build for normal configure and build commands. Use @ref
project_extending for integration rules covering new and changed repository
concepts.

## File ownership

| File | Owns |
| --- | --- |
| `CMakeLists.txt` | Root project composition and top-level target creation. |
| `CMakePresets.json` | Supported configure, build, and test presets. |
| `cmake/GameWIPOptions.cmake` | Project composition options and option compatibility checks. |
| `cmake/GameWIPPlatform.cmake` | Normalized platform resolution and required backend-file inclusion. |
| `cmake/GameWIPPackage.cmake` | Shared package configuration, version, and export installation. |
| `cmake/GameWIPApplication.cmake` | Explicit, single-resource application manifest attachment on Windows. |
| `cmake/GameWIPWarnings.cmake` | First-party C++ warning policy, including opt-in warnings as errors. |
| `cmake/LibraryDoxygen.cmake` | Doxygen input registration and generated documentation target creation. |
| `cmake/GameWIPDocumentation.cmake` | Project-level Doxygen page registration. |
| `cmake/GameWIPValidationModules.cmake` | Validation and benchmark module registration helpers. |
| `cmake/GameWIPRuntimeDependencies.cmake` | Runtime dependency staging and validation registration for executable targets. |
| `cmake/copy_runtime_dependencies.cmake` | Clean-shadow runtime dependency discovery and app-local DLL staging. |
| `cmake/ValidateRuntimeDependencies.cmake` | Windows/MSYS2 regression contract for compiler-runtime replacement and executable launch. |
| `cmake/GameWIPCoverage.cmake` | Coverage instrumentation and report target. |
| `cmake/GameWIPStaticAnalysis.cmake` | clang-tidy and clang-format validation targets. |
| `cmake/GameWIPSanitizers.cmake` | AddressSanitizer and UndefinedBehaviorSanitizer availability and instrumentation. |
| `cmake/GameWIPVersion.cmake` | Project version detection, display-version composition, and generated version header. |
| `cmake/RunInstalledConsumerValidation.cmake` | Clean installed-consumer package validation. |
| `cmake/ValidateExportedSymbols.cmake` | Shared-library exported-symbol allowlist validation. |
| `cmake/export_allowlists/` | Expected public exported-symbol roots for shared-library checks. |

Library-local CMake helpers belong under the owning library's `cmake/`
directory. The root `cmake/` folder is for policy shared by multiple libraries.

## Application manifests

Windows manifests are application policy. Libraries must not attach them through
`INTERFACE_SOURCES`, because a process can contain only one authoritative
manifest and linked libraries cannot know the executable's complete requirements.

Enable the resource compiler in the consuming project's top-level directory,
after `project()` and before adding subdirectories with Windows applications:

```cmake
if(WIN32)
    enable_language(RC)
endif()
```

The repository already performs this setup. Installed Assert and Desktop
packages load the shared helper through their `GameWIPApplication` dependency;
other installed consumers can request `GameWIPApplication` directly with
`find_package()` at the matching project version.

The helper accepts independent requirements and attaches one generated resource
to the final executable:

```cmake
gamewip_attach_application_manifest(
    TARGET MyApplication
    COMMON_CONTROLS_V6
    PER_MONITOR_V2
)
```

`COMMON_CONTROLS_V6` enables the preferred Assert dialog controls.
`PER_MONITOR_V2` is required by Desktop's Win32 window backend. The helper
merges repeated calls, rejects imported, alias, and non-executable targets, and
does nothing on non-Windows platforms. The same helper is installed through the
`GameWIPApplication` package so source-tree and installed consumers make the
same explicit application decision.

Calls may come from different directories. Generated files stay beneath the
owning target's binary directory, with distinct paths for distinct target names.
The application owns any other resources and must not attach a second process
manifest. Ordinary icon or version resources may coexist with this manifest.

## Presets and project options

Supported developer workflows start from `CMakePresets.json`. Presets set
intentional combinations of options from `cmake/GameWIPOptions.cmake`.

Project options use the `GAMEWIP_` prefix. They control repository composition,
instrumentation, validation, documentation, and tool workflows. Library-local
options use the owning library prefix when they are meaningful outside the root
project.

An option belongs in the CMake file that owns it. Set its value in every
relevant preset, describe project-facing behavior in @ref project_build or the
owning library manual, and add validation for meaningful enabled and disabled
behavior.

If one option requires another, express that relationship with an explicit CMake
check and a clear error message rather than hiding the coupling.

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

Maintained library sources use target-local include directories and compile
definitions, with explicit source lists instead of recursive discovery.

TestSupport remains a validation-oriented leaf relative to higher-level
reusable libraries. It may link foundational Unicode when its UTF-8 text
semantics require it, but it must not acquire IO, FileSystem, Terminal, Desktop,
Logger, Assert, engine, or other higher-level GameWIP dependencies for
convenience. Source-tree validation seams stay private to the libraries that
need them and are never part of installed target definitions or headers.

## Documentation helpers

Use `gamewip_register_doxygen_inputs()` for project-level Doxygen pages and
other explicit Doxygen inputs.

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

Use `gamewip_create_documentation_target()` only from the root project, after
all project and library Doxygen inputs have been registered.

Doxygen inputs are explicit by design. Recursive source-tree discovery would
hide missing registrations and is not used.

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

The CMake helper registers sources and dependencies. Runtime module name, order,
and adapter behavior remain in the module's `module.cpp` registration.

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

Benchmark modules remain separate from correctness tests. CMake registration
proves that a benchmark is available; benchmark pages explain how to collect
meaningful measurements.

## Platform backends

Use `gamewip_resolve_platform_id()` once at root configuration time to determine
the project platform ID.

Use `gamewip_target_platform_backend(TARGET <target> ROOT <platform-root>)`
from every platform-aware target. The helper includes the active backend's
`platform.cmake`; that file owns backend sources, system libraries, resources,
private includes, and compile definitions.

The normal backend shape is:

```text
<library>/platform/<platform-id>/
  platform.cmake
  <platform-id>_<feature>.cpp
```

Platform backend behavior is governed by @ref project_platform_backend_contract.

## Runtime dependencies

Use `gamewip_copy_runtime_dependencies(<target>)` for executable targets that
must run directly from the build tree on the supported Windows/MSYS2
environment.

The helper adds a post-build step that resolves runtime DLLs and copies them
beside the target executable. It lets validation executables and the game run
without asking users to copy compiler runtime dependencies manually.

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

These helpers are controlled by presets and `GAMEWIP_` options. Individual
library CMake files must not enable coverage, static analysis, or sanitizer
behavior unless a library-local option has an explicitly documented reason.

## Version and package validation

`gamewip_configure_version()` generates the build identity used by runtime
diagnostics and documentation. Version policy is documented in @ref
project_versioning.

Package-boundary validation is part of the validation workflow. Changes to
install rules, exported targets, package config files, exact version files, or
public dependency visibility must keep clean installed-consumer validation
working. Combined and isolated consumers must also reject source-tree-only
test-hook definitions exposed by libraries that retain them.

Shared-library exported-symbol checks use allowlists under
`cmake/export_allowlists/`. An allowlist changes only when the public exported
surface intentionally changes.

## Shared helper conventions

Target-local properties and explicit source registration keep CMake state easy to
trace. Project composition options belong in root infrastructure, while
library-local behavior stays with the owning library. Platform selection goes
through `GameWIPPlatform.cmake`, and target wiring belongs in each backend's
`platform.cmake`. Doxygen inputs, validation modules, and benchmarks use the
project helpers. Installed public headers remain free of internal and test-hook
paths. New maintainer-facing helpers need documentation before they are used
widely.

## Change validation

For a CMake infrastructure change, the pull request must show that:

- Behavior still lives in its owning file.
- Affected presets configure successfully.
- Target dependencies use the narrowest correct visibility.
- Package changes pass installed-consumer validation.
- Doxygen inputs remain explicitly registered.
- Validation and benchmark modules use the helper layer.
- Backend changes preserve the platform contract.
- The configure, build, test, or documentation commands supporting the change are recorded.

## Related pages

- @ref project_build
- @ref project_extending
- @ref project_documentation
- @ref project_platform_backend_contract
- @ref project_library_compatibility
