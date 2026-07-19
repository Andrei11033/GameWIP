@page logger_abi Package and ABI boundary

Logger is a shared C++ library with an exact-version CMake package.

## Installed package

Set `GAMEWIP_REQUIRED_VERSION` from the consuming project's dependency lock; see @ref project_library_compatibility.

```cmake
find_package(Logger ${GAMEWIP_REQUIRED_VERSION} EXACT CONFIG REQUIRED)
target_link_libraries(MyTarget PRIVATE GameWIP::Logger)
```

The package installs:

- `logger/logger.h`;
- `logger/logger_macros.h`;
- generated `logger/logger_export.h`;
- the imported `GameWIP::Logger` target.

The package resolves its public runtime dependency on the exact matching Terminal package. FileSystem and IO are private implementation dependencies of Logger's installed target.

## Shared-library exports

`logger_export.h` supplies the platform visibility/import macros used by exported non-template functions. Public templates in `logger.h` call exported `GameWIP::Logger::Detail::Core` bridge functions for formatting, queueing, and reporting.

Those `Detail::Core` symbols are binary support for the public inline/template API. They are not supported source-level consumer APIs and must not be called directly.

Test-hook exports exist only in source-tree builds where `LOGGER_ENABLE_TEST_HOOKS` enables them. Their header is not installed and they are not a versioned consumer interface.

## Compatibility expectations

The public boundary exposes C++ standard-library types, templates, inline code, enum and structure layout, `std::format`, spans, strings, chrono durations, and exceptions. Consumers must follow @ref project_library_compatibility, including exact package version and compatible compiler, standard library, language mode, and runtime settings.

Compatibility-relevant changes include:

- public enum values or structure layout;
- function signatures, calling convention, or export set;
- inline/template implementation requirements;
- macro behavior;
- dependency and runtime-library requirements.

The textual log format, documented source/filter semantics, queue/drop rules, and report behavior are public behavioral contracts, but internal queue layout, worker implementation, timestamp cache, scratch-container choice, and private dependency wiring are not ABI promises.

## Source-tree-only interfaces

The short `Logger` target, `logger/internal/*`, implementation sources, validation hooks, and test/benchmark code are repository interfaces. Installed consumers should use only the imported target and public headers.

## Related pages

- @ref logger_public_api
- @ref logger_configuration
- @ref logger_testing
