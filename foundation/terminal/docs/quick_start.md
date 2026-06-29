@page terminal_quick_start Terminal quick start

Terminal exposes explicit operations for stdin, stdout, stderr, styling, and primitive controls.

## Include and link

Include the public header:

```cpp
#include "terminal/terminal.h"
```

Link the shared `Terminal` target. Terminal publicly depends on `IO` because its API uses shared
status, write-result, and flush-mode types. The shared runtime keeps one standard-stream state across
the application and its libraries.

## Write a line

```cpp
GameWIP::Terminal::writeLine("Hello");
```

## Write to stderr

```cpp
GameWIP::Terminal::writeLine(GameWIP::Terminal::Types::OutputStream::Stderr, "error");
```

## Read a line

```cpp
const auto result = GameWIP::Terminal::readLine();
if (result.status.ok() && result.outcome == GameWIP::Terminal::Types::ReadOutcome::Completed)
{
    GameWIP::Terminal::writeLine("input: " + result.line);
}
```

## Batch output

Use `OutputBuffer` or `writeSegments()` when a caller naturally builds many small output pieces. See @ref terminal_read_write and @ref terminal_segmented_writes.
