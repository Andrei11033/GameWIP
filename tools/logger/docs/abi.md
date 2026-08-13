@page logger_abi Package and ABI boundary

Logger is a shared C++ library with an exact-version CMake package.

## Installed package

```cmake
find_package(Logger ${GAMEWIP_REQUIRED_VERSION} EXACT CONFIG REQUIRED)
target_link_libraries(MyTarget PRIVATE GameWIP::Logger)
```

The package installs `logger/types.h`, `logger/config.h`, `logger/logger.h`, `logger/logger_macros.h`, the generated export header, and `GameWIP::Logger`.

`logger.h` exposes `IO::Types::Status`, so IO is a public package dependency and `LoggerConfig.cmake` resolves the exact matching IO package. Terminal remains an installed dependency used by Logger's console implementation. FileSystem and Unicode are private implementation dependencies.

## Shared-library exports

Public templates in `logger.h` call exported `GameWIP::Logger::Detail::Core` bridges. Those symbols support the public inline/template API but are not source-level consumer APIs.

This standardization pass is an intentional pre-1.0 API/ABI break: initialization, report, and health vocabulary is organized under `Types::Init`, `Types::Report`, and `Types::Health`; runtime filter reset operations use `reset*Filter`; and no compatibility aliases are installed.

## Compatibility expectations

The public boundary exposes standard-library types, templates, inline code, enums and structures. Consumers use the exact matching package version and compatible compiler/runtime settings.

Compatibility-relevant changes include public enum/structure layout, signatures, export sets, template requirements, macro behavior, and dependency requirements. Internal queue layout, worker implementation, timestamp cache, and private dependency wiring are not ABI promises.

## Source-tree-only interfaces

The short `Logger` target, `logger/internal/*`, implementation sources, hooks, tests, and benchmarks are repository interfaces only.

## Related pages

- @ref logger_public_api
- @ref logger_configuration
- @ref logger_testing
