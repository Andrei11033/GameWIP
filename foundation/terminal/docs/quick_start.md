@page terminal_quick_start Terminal quick start

Terminal exposes checked operations for stdin, stdout, stderr, styling, and primitive controls. It has no explicit initialization or shutdown lifecycle.

## Include

```cpp
#include "terminal/terminal.h"
```

## Installed CMake

Set `GAMEWIP_REQUIRED_VERSION` from the consuming project's dependency lock; see @ref project_library_compatibility.

```cmake
find_package(Terminal ${GAMEWIP_REQUIRED_VERSION} EXACT CONFIG REQUIRED)
target_link_libraries(MyTarget PRIVATE GameWIP::Terminal)
```

The package resolves its exact-version IO dependency.

## Source-tree CMake

```cmake
target_link_libraries(MyTarget PRIVATE Terminal)
```

## Minimal checked output

```cpp
#include "terminal/terminal.h"

int main()
{
    const auto status = GameWIP::Terminal::writeLine("Hello from GameWIP");
    return status.ok() ? 0 : 1;
}
```

Select stderr explicitly:

```cpp
const auto status = GameWIP::Terminal::writeLine(
    GameWIP::Terminal::Types::OutputStream::Stderr,
    "configuration failed");
```

## Minimal checked input

```cpp
const auto result = GameWIP::Terminal::readLine();
if (!result.status.ok())
{
    // Backend, validation, or encoding failure.
}
else
{
    switch (result.outcome)
    {
    case GameWIP::Terminal::Types::ReadOutcome::Completed:
        // result.line contains a completed line.
        break;
    case GameWIP::Terminal::Types::ReadOutcome::EndOfStream:
        // result.line may still contain a final unterminated line.
        break;
    case GameWIP::Terminal::Types::ReadOutcome::TimedOut:
    case GameWIP::Terminal::Types::ReadOutcome::WouldBlock:
        // No normal backend failure occurred. Partial text may still be present.
        break;
    }
}
```

Always inspect `status`, `outcome`, and the payload together. A successful status does not imply `ReadOutcome::Completed`, and a terminating outcome can accompany partial data.

## Capability-aware styling

```cpp
using namespace GameWIP::Terminal;

Types::LineWriteOptions options;
options.styleMode = Types::StyleMode::Auto;
options.style.foreground = basicColor(Types::BasicColor::Green);
options.style.bold = true;

const auto status = writeLine("ready", options);
```

`StyleMode::Auto` falls back to plain text. Use `StyleMode::Required` only when lack of the requested style must fail the operation.

## Failure handling

Expected validation, detached-stream, unsupported-operation, encoding, timeout, and backend failures use IO status/result types. Check the status before relying on payload fields.

Some in-memory preparation remains ordinary C++ code and may throw. In particular, `OutputBuffer` allocation and formatting operations can propagate standard exceptions. Scope factories are `noexcept`; inspect the returned scope's `status()` and `active()` state to determine whether setup succeeded.

## Where to go next

- @ref terminal_public_api maps the complete public surface.
- @ref terminal_read_write owns read, write, buffering, exception, and concurrency contracts.
- @ref terminal_capabilities_and_redirection explains endpoint-dependent behavior.
- @ref terminal_styling and @ref terminal_segmented_writes cover styled and batched output.
- @ref terminal_input_modes and @ref terminal_control_primitives cover temporary terminal state.
- @ref terminal_examples contains complete integration examples.
