@page terminal_quick_start Terminal quick start

Terminal exposes explicit operations for stdin, stdout, stderr, styling, and primitive controls.

## Include

Include the library's public header:

```cpp
#include "terminal/terminal.h"
```

## Installed CMake

Use the package's namespaced imported target. The package resolves its IO dependency:

```cmake
find_package(Terminal CONFIG REQUIRED)
target_link_libraries(MyTarget PRIVATE GameWIP::Terminal)
```

## Source-tree CMake

When Terminal is part of the same source tree, use its short build target:

```cmake
target_link_libraries(MyTarget PRIVATE Terminal)
```

Terminal is a shared library. Its process-wide runtime coordinates standard-stream operations across the application and other libraries that use Terminal.

## Minimal usage

### Write a line

```cpp
GameWIP::Terminal::writeLine("Hello");
```

### Write to stderr

```cpp
GameWIP::Terminal::writeLine(GameWIP::Terminal::Types::OutputStream::Stderr, "error");
```

### Read a line

```cpp
const auto result = GameWIP::Terminal::readLine();
if (result.status.ok() && result.outcome == GameWIP::Terminal::Types::ReadOutcome::Completed)
{
    GameWIP::Terminal::writeLine("input: " + result.line);
}
```

### Batch output

Use `OutputBuffer` or `writeSegments()` when a caller naturally builds many small output pieces. See @ref terminal_read_write and @ref terminal_segmented_writes.
