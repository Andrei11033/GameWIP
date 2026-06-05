@page gamewip_build Build, packages, and installed docs

This page covers project-level build behavior. Library-specific usage is documented under the foundation and tool library pages, including @ref io, @ref terminal, @ref logger, @ref assert, and @ref test_support.

## Normal source build

By default, GameWIP builds the foundation/tool libraries from the source tree:

```powershell
cmake -S . -B build -G Ninja `
  -DASSERT_ENABLED=ON `
  -DASSERT_CHECKS_ENABLED=ON `
  -DGAMEWIP_ENABLE_LOGGER_TEST_HOOKS=OFF `
  -DGAMEWIP_ENABLE_ASSERT_TEST_HOOKS=OFF `
  -DGAMEWIP_ENABLE_TERMINAL_TEST_HOOKS=OFF `
  -DGAMEWIP_ENABLE_COVERAGE=OFF `
  -DGAMEWIP_BUILD_DOCS=OFF

cmake --build build
```

Runtime test selection stays in `TestRunOptions` in `game/main.cpp`; CMake options control build-time features only.

## Using installed packages

After the libraries have been installed, an external CMake project can consume them with:

```cmake
find_package(GameWIP_IO CONFIG REQUIRED)
find_package(GameWIP_Terminal CONFIG REQUIRED)
find_package(GameWIP_Logger CONFIG REQUIRED)
find_package(GameWIP_Assert CONFIG REQUIRED)
find_package(GameWIP_TestSupport CONFIG REQUIRED)

target_link_libraries(SomeTarget PRIVATE
    GameWIP::IO
    GameWIP::Terminal
    GameWIP::Logger
    GameWIP::Assert
    GameWIP::TestSupport
)
```

The installed package config files are:

```text
GameWIP_IOConfig.cmake
GameWIP_IOConfigVersion.cmake
GameWIP_TerminalConfig.cmake
GameWIP_TerminalConfigVersion.cmake
GameWIP_LoggerConfig.cmake
GameWIP_LoggerConfigVersion.cmake
GameWIP_AssertConfig.cmake
GameWIP_AssertConfigVersion.cmake
GameWIP_TestSupportConfig.cmake
GameWIP_TestSupportConfigVersion.cmake
```

`GameWIP_TerminalConfig.cmake` depends on the IO package through `find_dependency(GameWIP_IO CONFIG)`. `GameWIP_AssertConfig.cmake` depends on the Logger package through `find_dependency(GameWIP_Logger CONFIG)`. Internal test-hook headers are intentionally excluded from normal installs.

On Windows, `GameWIP::Assert` also propagates the Common Controls v6 manifest resource needed by the TaskDialog path. Link the imported target normally; no separate package-consumer mode is required.

## Install layout

A normal install provides headers, libraries, and package config files similar to:

```text
<prefix>/include/io/...
<prefix>/include/terminal/...
<prefix>/include/logger/...
<prefix>/include/debug/assert/...
<prefix>/include/test_support/...
<prefix>/lib/cmake/GameWIP_IO/GameWIP_IOConfig.cmake
<prefix>/lib/cmake/GameWIP_Terminal/GameWIP_TerminalConfig.cmake
<prefix>/lib/cmake/GameWIP_Logger/GameWIP_LoggerConfig.cmake
<prefix>/lib/cmake/GameWIP_Assert/GameWIP_AssertConfig.cmake
<prefix>/lib/cmake/GameWIP_TestSupport/GameWIP_TestSupportConfig.cmake
```

The Terminal package depends on the IO package through `find_dependency(GameWIP_IO CONFIG)`. The Assert package depends on the Logger package through `find_dependency(GameWIP_Logger CONFIG)`. The TestSupport package is independent and has no Logger or Assert dependency.

## Doxygen build

Doxygen is opt-in:

```powershell
cmake -S . -B build-docs -G Ninja `
  -DGAMEWIP_BUILD_DOCS=ON

cmake --build build-docs --target docs
```

Generated HTML is written to:

```text
build-docs/docs/doxygen/html/index.html
```

Normal builds do not require Doxygen. When `GAMEWIP_BUILD_DOCS=ON`, CMake fails early with a clear error if Doxygen is missing.

## Installing generated docs

Generated HTML docs can also be installed:

```powershell
cmake -S . -B build-install-docs `
  -DGAMEWIP_BUILD_DOCS=ON `
  -DGAMEWIP_INSTALL_DOCS=ON `
  -DCMAKE_INSTALL_PREFIX=D:/GameWIP/install-test

cmake --build build-install-docs --target docs
cmake --install build-install-docs
```

The docs target must be built before install. Installed HTML goes to:

```text
<prefix>/share/doc/GameWIP/doxygen/html/index.html
```
