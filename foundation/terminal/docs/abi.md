@page terminal_abi Package and ABI boundary

Terminal is distributed as a shared C++ library with an exact-version CMake package.

## Installed use

Set `GAMEWIP_REQUIRED_VERSION` from the consuming project's dependency lock; see @ref project_library_compatibility.

```cmake
find_package(Terminal ${GAMEWIP_REQUIRED_VERSION} EXACT CONFIG REQUIRED)
target_link_libraries(MyTarget PRIVATE GameWIP::Terminal)
```

The package resolves `IO` with the same exact project version. Source-tree targets may link the short `Terminal` target.

## Installed public surface

The package installs:

- `terminal/terminal.h`;
- generated `terminal/terminal_export.h`;
- the `GameWIP::Terminal` imported target and package metadata.

Internal platform headers and test hooks are not installed.

## Binary compatibility

The public interface exposes C++ standard-library types including `std::string`, `std::string_view`, `std::span`, `std::format_string`, `std::chrono::milliseconds`, and standard exceptions. Consumers must follow the project compiler, language-mode, standard-library, runtime-library, architecture, and build-configuration compatibility policy. See @ref project_library_compatibility.

The exact-version package file prevents CMake from treating a different GameWIP release as package-compatible. It does not make arbitrary compiler or runtime combinations ABI-compatible.

## Process-wide runtime

All modules in one process must resolve the same Terminal shared library. Its process-wide stdin/stdout/stderr state provides the synchronization and scope-nesting behavior documented by the manual. Statically duplicating or privately embedding separate Terminal runtimes would not provide one shared coordination domain.

## Exported template bridges

Public `print()` and `println()` templates package arguments into `std::format_args` and call exported `GameWIP::Terminal::Detail::vprint()` and `vprintln()` functions. These `Detail` symbols are ABI support for the public templates, not consumer API. Consumers must call the public templates instead of depending on the bridge signatures directly.

## Dependency direction

Terminal publicly depends on IO because statuses, flush modes, and byte-write results appear in its public API. Logger depends on Terminal for console output. Terminal must not depend on Logger or higher-level runtime systems.

## Test hooks

When enabled in a source-tree validation build, test hooks add exported build-tree symbols for deterministic tests. They are not installed, are not versioned consumer API, and must not be used by production targets. See @ref terminal_test_hooks.
