@page library_build Build, packages, and installed docs

This page covers project-level build behavior. Library-specific usage is documented under the foundation and tool library pages, including @ref io, @ref terminal, @ref logger, @ref assert, and @ref test_support.

## Normal source build

By default, the project builds the foundation and tool libraries from the source tree:

```powershell
cmake -S . -B build -G Ninja `
  -DASSERT_ENABLED=ON `
  -DASSERT_CHECKS_ENABLED=ON `
  -DLOGGER_TEST_HOOKS=OFF `
  -DASSERT_TEST_HOOKS=OFF `
  -DTERMINAL_TEST_HOOKS=OFF `
  -DENABLE_LIBRARY_COVERAGE=OFF `
  -DBUILD_DOCS=OFF

cmake --build build
```

Runtime test selection stays in `TestRunOptions` in `game/main.cpp`; CMake options control build-time features only.

## Using installed packages

After the libraries have been installed, an external CMake project can consume them with:

```cmake
find_package(IO CONFIG REQUIRED)
find_package(Terminal CONFIG REQUIRED)
find_package(Logger CONFIG REQUIRED)
find_package(Assert CONFIG REQUIRED)
find_package(TestSupport CONFIG REQUIRED)

target_link_libraries(SomeTarget PRIVATE
    IO
    Terminal
    Logger
    Assert
    TestSupport
)
```

The installed package config files are:

```text
IOConfig.cmake
IOConfigVersion.cmake
TerminalConfig.cmake
TerminalConfigVersion.cmake
LoggerConfig.cmake
LoggerConfigVersion.cmake
AssertConfig.cmake
AssertConfigVersion.cmake
TestSupportConfig.cmake
TestSupportConfigVersion.cmake
```

`TerminalConfig.cmake` depends on the IO package through `find_dependency(IO CONFIG)`. `AssertConfig.cmake` depends on the Logger package through `find_dependency(Logger CONFIG)`. Internal test-hook headers are intentionally excluded from normal installs.

On Windows, `Assert` also propagates the Common Controls v6 manifest resource needed by the TaskDialog path. Link the imported target normally; no separate package-consumer mode is required.

## Install layout

A normal install provides headers, libraries, and package config files similar to:

```text
<prefix>/include/io/...
<prefix>/include/terminal/...
<prefix>/include/logger/...
<prefix>/include/debug/assert/...
<prefix>/include/test_support/...
<prefix>/lib/cmake/IO/IOConfig.cmake
<prefix>/lib/cmake/Terminal/TerminalConfig.cmake
<prefix>/lib/cmake/Logger/LoggerConfig.cmake
<prefix>/lib/cmake/Assert/AssertConfig.cmake
<prefix>/lib/cmake/TestSupport/TestSupportConfig.cmake
```

The Terminal package depends on the IO package through `find_dependency(IO CONFIG)`. The Assert package depends on the Logger package through `find_dependency(Logger CONFIG)`. The TestSupport package is independent and has no Logger or Assert dependency.

## Doxygen build

Doxygen is opt-in:

```powershell
cmake -S . -B build-docs -G Ninja `
  -DBUILD_DOCS=ON

cmake --build build-docs --target docs
```

Generated HTML is written to:

```text
build-docs/docs/doxygen/html/index.html
```

Normal builds do not require Doxygen. When `BUILD_DOCS=ON`, CMake fails early with a clear error if Doxygen is missing.

## Installing generated docs

Generated HTML docs can also be installed:

```powershell
cmake -S . -B build-install-docs `
  -DBUILD_DOCS=ON `
  -DINSTALL_DOCS=ON `
  -DCMAKE_INSTALL_PREFIX=D:/library-install-test

cmake --build build-install-docs --target docs
cmake --install build-install-docs
```

The docs target must be built before install. Installed HTML goes to:

```text
<prefix>/share/doc/Libraries/doxygen/html/index.html
```
