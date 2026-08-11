@page terminal_examples Examples

The examples include the complete local setup required for the operation being demonstrated. Production code should propagate or report richer status detail according to its own error policy.

## Checked stdout and stderr

```cpp
#include "terminal/terminal.h"

int main()
{
    using GameWIP::Terminal::Types::OutputStream;

    const auto first = GameWIP::Terminal::writeLine("starting");
    if (!first.ok())
    {
        return 1;
    }

    const auto second = GameWIP::Terminal::writeLine(OutputStream::Stderr, "diagnostic");
    return second.ok() ? 0 : 1;
}
```

## Read a line and handle every outcome

```cpp
#include "terminal/terminal.h"

int main()
{
    using namespace GameWIP::Terminal;

    const Types::LineReadResult result = readLine();
    if (!result.status.ok())
    {
        return 1;
    }

    if (!result.line.empty())
    {
        if (!writeLine("received: " + result.line).ok())
        {
            return 1;
        }
    }

    switch (result.outcome)
    {
    case Types::ReadOutcome::Completed:
        return 0;
    case Types::ReadOutcome::EndOfStream:
        return 2;
    case Types::ReadOutcome::TimedOut:
        return 3;
    case Types::ReadOutcome::WouldBlock:
        return 4;
    case Types::ReadOutcome::Cancelled:
        return 5;
    }

    return 6;
}
```

## Non-blocking byte input when supported

```cpp
#include "terminal/terminal.h"

#include <array>
#include <cstddef>
#include <span>

int main()
{
    using namespace GameWIP::Terminal;

    const auto capabilities = getInputCapabilities();
    if (!capabilities.status.ok() || !capabilities.capabilities.supportsNonBlockingReads)
    {
        return 0;
    }

    std::array<std::byte, 256> storage{};
    Types::ByteReadOptions options;
    options.timeout = kNoWait;

    const auto result = readBytes(std::span<std::byte>{storage}, options);
    return result.status.ok() ? 0 : 1;
}
```

## Styled output with fallback

```cpp
#include "terminal/terminal.h"

int main()
{
    using namespace GameWIP::Terminal;

    Types::LineWriteOptions options;
    options.styleMode = Types::StyleMode::Auto;
    options.style.foreground = basicColor(Types::BasicColor::Green);
    options.style.bold = true;

    return writeLine("ready", options).ok() ? 0 : 1;
}
```

## Segmented output with stable payload storage

```cpp
#include "terminal/terminal.h"

#include <array>
#include <string>

int main()
{
    using namespace GameWIP::Terminal;

    const std::string label = "[info] ";
    const std::string message = "loaded";

    Types::TextStyle emphasis;
    emphasis.foreground = basicColor(Types::BasicColor::Cyan);

    const std::array segments{
        styledTextSegment(label, emphasis),
        textSegment(message),
    };

    Types::SegmentWriteOptions options;
    options.appendLineEnding = true;

    return writeSegments(segments, options).ok() ? 0 : 1;
}
```

The strings remain alive until `writeSegments()` returns.

## Reusable OutputBuffer with retry

```cpp
#include "terminal/terminal.h"

int main()
{
    using namespace GameWIP::Terminal;

    OutputBuffer buffer(Types::LineEnding::Lf);
    buffer.reserve(1024);
    buffer.println("entity {} hp {}", 7, 95);
    buffer.println("entity {} hp {}", 8, 42);

    const auto status = buffer.flushTo();
    if (!status.ok())
    {
        // The text is preserved, so a higher layer may retry or copy it elsewhere.
        return buffer.empty() ? 2 : 1;
    }

    return buffer.empty() ? 0 : 3;
}
```

## Persistent Stream session with explicit restoration

```cpp
#include "terminal/terminal.h"

int main()
{
    using namespace GameWIP::Terminal;

    Session session;
    Types::SessionOptions options;
    options.deliveryMode = Types::InputDeliveryMode::Stream;

    const auto openStatus = session.open(options);
    if (!openStatus.ok())
    {
        return 1;
    }

    const Types::LineReadResult line = session.readLine();
    const auto closeStatus = session.close();

    if (!line.status.ok() || !closeStatus.ok())
    {
        return 2;
    }

    return line.outcome == Types::ReadOutcome::Completed ? 0 : 3;
}
```

Explicit `close()` is preferred when native restoration failure matters. A failed close retains session ownership for retry.

## Persistent structured-event session

```cpp
#include "terminal/terminal.h"

int main()
{
    using namespace GameWIP::Terminal;

    Session session; // Events is the default delivery mode.
    if (!session.open().ok())
    {
        return 1;
    }

    const Types::EventReadResult result = session.readEvent();
    if (!result.status.ok())
    {
        static_cast<void>(session.close());
        return 2;
    }

    if (result.outcome == Types::ReadOutcome::Completed && result.event.has_value())
    {
        if (const Types::ResizeEvent *resize = result.event->getIf<Types::ResizeEvent>())
        {
            // resize->size uses the same viewport semantics as getTerminalSize().
        }
    }

    return session.close().ok() ? 0 : 3;
}
```

On native Win32 consoles, key/repeat/release/modifier/location and resize events come from `INPUT_RECORD`. Mouse records are ignored until Terminal has a deliberate portable mouse contract.

## Cancel a managed read

```cpp
#include "terminal/terminal.h"

#include <stop_token>

int main()
{
    using namespace GameWIP::Terminal;

    std::stop_source source;
    source.request_stop();

    Types::TextReadOptions options;
    options.stopToken = source.get_token();

    const Types::TextReadResult result = readText(options);
    return result.status.ok() &&
                   result.outcome == Types::ReadOutcome::Cancelled
               ? 0
               : 1;
}
```

A pre-requested stop never consumes input. For cancellation of an in-progress blocking read, first check `supportsCancellation`.

## Cursor-hidden scope

```cpp
#include "terminal/terminal.h"

int main()
{
    auto hidden = GameWIP::Terminal::scopedCursorHidden();
    if (!hidden.status().ok())
    {
        return 1;
    }

    const auto writeStatus = GameWIP::Terminal::writeLine("working...");
    const auto restoreStatus = hidden.restore();
    return writeStatus.ok() && restoreStatus.ok() ? 0 : 2;
}
```

## Capability-aware terminal size

```cpp
#include "terminal/terminal.h"

int main()
{
    using namespace GameWIP::Terminal;

    const auto capabilities = getOutputCapabilities();
    if (!capabilities.status.ok() || !capabilities.capabilities.supportsTerminalSize)
    {
        return 0;
    }

    const auto size = getTerminalSize();
    if (!size.status.ok())
    {
        return 1;
    }

    return println("{} columns x {} rows", size.size.columns, size.size.rows).ok() ? 0 : 2;
}
```

## Raw bytes to a suitable endpoint

```cpp
#include "terminal/terminal.h"

#include <array>
#include <cstddef>
#include <span>

int main()
{
    using namespace GameWIP::Terminal;

    const auto capabilities = getOutputCapabilities();
    if (!capabilities.status.ok() || !capabilities.capabilities.supportsByteOutput)
    {
        return 0;
    }

    const std::array payload{std::byte{'O'}, std::byte{'K'}, std::byte{'\n'}};
    const auto result = writeBytes(std::span<const std::byte>{payload});
    return result.status.ok() && result.bytesWritten == payload.size() ? 0 : 1;
}
```

See @ref terminal_read_write for result interpretation and @ref terminal_capabilities_and_redirection for endpoint differences.
