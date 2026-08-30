@page terminal_quick_start Quick start

Terminal exposes checked direct operations for stdin, stdout, stderr, styling, and primitive controls. Persistent interactive applications use an
explicit move-only `Session` that owns input and binds one primary output; direct reads acquire and restore managed input ownership automatically.

## Include

```cpp
#include "terminal/terminal.h"
```

`terminal/terminal.h` is the complete convenience include. Narrow consumers may
use `terminal/types.h`, `terminal/style.h`, `terminal/input.h`,
`terminal/output.h`, or `terminal/session.h`. Header choice does not alter
Terminal behavior.

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
    GameWIP::Terminal::Types::Output::Stream::Stderr,
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
    case GameWIP::Terminal::Types::Input::ReadOutcome::Completed:
        // result.line contains a completed line.
        break;
    case GameWIP::Terminal::Types::Input::ReadOutcome::EndOfStream:
        // result.line may still contain a final unterminated line.
        break;
    case GameWIP::Terminal::Types::Input::ReadOutcome::TimedOut:
    case GameWIP::Terminal::Types::Input::ReadOutcome::WouldBlock:
        // No normal backend failure occurred. Partial text may still be present.
        break;
    case GameWIP::Terminal::Types::Input::ReadOutcome::Cancelled:
        // The caller's stop token requested cancellation.
        break;
    }
}
```

Always inspect `status`, `outcome`, and the payload together. A successful status does not imply `Types::Input::ReadOutcome::Completed`, and a
terminating outcome can accompany partial data.

Read deadlines use `std::optional<std::chrono::milliseconds>`: `std::nullopt` waits indefinitely, `0ms` polls, positive values bound the complete
operation, and negative values are `InvalidArgument`. A `std::stop_token` requests cancellation where the endpoint supports cancellable blocking
reads.

## Persistent managed session

```cpp
using namespace GameWIP::Terminal;

Session session;
Types::SessionOptions options;
options.deliveryMode = Types::Input::DeliveryMode::Stream;

if (!session.open(options).ok())
{
    return 1;
}

if (!session.writeText("command: ").ok())
{
    static_cast<void>(session.close());
    return 2;
}

Types::Input::LineOptions lineOptions;
lineOptions.echo = true;
const Types::Input::LineResult line = session.readLine(lineOptions);
if (line.status.ok() && line.outcome == Types::Input::ReadOutcome::Completed)
{
    static_cast<void>(session.println("received {}", line.line));
}

const auto closeStatus = session.close();
```

Interactive `readLine()` owns Unicode editing and echo rather than delegating to native cooked input. Set `lineOptions.echo = false` for password-like
or application-rendered input.

Only one managed owner may consume stdin at a time. Re-opening the same object returns `AlreadyOpen`; a competing session or direct read returns
`ResourceBusy`. Explicit `close()` reports restoration failures and retains ownership when restoration can be retried. It also waits for active
Session calls; if formatter code tries to close its own Session operation, that nested `close()` returns `ResourceBusy` without changing the open
state.

## Structured event input

```cpp
using namespace GameWIP::Terminal;

Session session;
if (!session.open().ok()) // Events is the explicit-session default.
{
    return 1;
}

const Types::Input::EventResult event = session.readEvent();
if (event.status.ok() && event.outcome == Types::Input::ReadOutcome::Completed)
{
    if (const Types::Events::Key *key = event.event->getIf<Types::Events::Key>())
    {
        // Portable logical key; no Win32 virtual-key code escapes the backend.
    }
}

const auto closeStatus = session.close();
```

Real Win32 consoles deliver key and resize events through native `INPUT_RECORD`. Mouse records are intentionally ignored today, but the backend
dispatcher keeps a separate mouse branch so a future portable mouse contract can be added without redesigning the reader.

## Capability-aware styling

```cpp
using namespace GameWIP::Terminal;

Types::Output::LineOptions options;
options.styleMode = Types::Style::Mode::Auto;
options.style.foreground = basicColor(Types::Style::BasicColor::Green);
options.style.bold = true;

const auto status = writeLine("ready", options);
```

`Types::Style::Mode::Auto` falls back to plain text. Use `Types::Style::Mode::Required` only when lack of the requested style must fail the operation.

## Failure handling

Expected validation, detached-stream, unsupported-operation, encoding, timeout, and backend failures use IO status/result types. Check the status
before relying on payload fields.

Checked Terminal operations contain failures from Terminal-owned allocation, formatting, conversion, and backend work. `OutputBuffer` allocating
mutations/formatting and the complete Session lifecycle/input/output/control surface use status-returning non-throwing contracts. Caller-owned
argument construction that occurs before Terminal receives control remains ordinary C++ behavior.

## Where to go next

- @ref terminal_public_api maps the complete public surface.
- @ref terminal_read_write owns read, write, buffering, exception, and concurrency contracts.
- @ref terminal_capabilities_and_redirection explains endpoint-dependent behavior.
- @ref terminal_styling and @ref terminal_segmented_writes cover styled and batched output.
- @ref terminal_input_modes covers persistent managed input ownership and session restoration; @ref terminal_control_primitives covers output control
  state.
- @ref terminal_examples contains complete integration examples.
