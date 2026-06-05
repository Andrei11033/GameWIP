@page terminal_getting_started Terminal getting started

## Include and link

Include:

```cpp
#include "terminal/terminal.h"
```

Link the `Terminal` target. Terminal publicly depends on `IO` because it uses shared status, write-result, and flush-mode types.

## Common path

Use free functions for one-off operations:

```cpp
GameWIP::Terminal::writeLine("ready");
```

Use `Reader` or `Writer` when a caller repeatedly targets the same stream:

```cpp
GameWIP::Terminal::Writer errorWriter(GameWIP::Terminal::Types::OutputStream::Stderr);
errorWriter.writeLine("startup failed");
```

For behavior details, use @ref terminal_public_api and the focused pages for reads, writes, styling, controls, and input modes.
