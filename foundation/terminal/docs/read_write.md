@page terminal_read_write Terminal read and write

## Streams

Terminal input currently exposes `Types::InputStream::Stdin`.

Terminal output exposes:

- `Types::OutputStream::Stdout`;
- `Types::OutputStream::Stderr`.

Free read calls default to stdin. Free write calls default to stdout when no output stream is supplied. Stderr is selected explicitly:

```cpp
GameWIP::Terminal::writeLine(GameWIP::Terminal::Types::OutputStream::Stderr, "error text");
```

## Read Calls

Terminal reads use explicit operation names.

| Operation | Shape | Result |
| --- | --- | --- |
| Line read | `readLine()` or `readLine(LineReadOptions{})` | `Types::LineReadResult` |
| Stream line read | `readLine(InputStream::Stdin, LineReadOptions{})` | `Types::LineReadResult` |
| Text chunk read | `readText()` or `readText(TextReadOptions{})` | `Types::TextReadResult` |
| Stream text chunk read | `readText(InputStream::Stdin, TextReadOptions{})` | `Types::TextReadResult` |
| Byte read | `readBytes(std::span<std::byte>{...})` | `Types::ByteReadResult` |
| Stream byte read | `readBytes(InputStream::Stdin, std::span<std::byte>{...})` | `Types::ByteReadResult` |

`readLine()` with no arguments reads one line from stdin. `readText()` reads one available UTF-8 chunk. `readBytes()` reads into caller-owned storage.

Text chunk reads wait according to `TextReadOptions::timeout`. Once input becomes available, they read one practical available UTF-8 chunk, return no more than `TextReadOptions::maxBytes`, and preserve valid UTF-8 code point boundaries. The chunk boundary and terminating outcome define what is returned.

Byte reads use caller-owned storage. `ByteReadOptions::allowPartial = true` allows a successful read to return after any positive byte count. `allowPartial = false` means the operation attempts to fill the buffer unless an outcome or failure stops it.

Line reads do not expose `allowPartial`. They wait for a line ending, end-of-stream, timeout, would-block result, truncation, or failure. If a timeout or end-of-stream occurs after line text has been read but before a line ending, the result returns the partial line text together with the terminating outcome.

## Read results

Read results separate expected read outcomes from backend failures.

| Situation | `status` | `outcome` |
| --- | --- | --- |
| Data read normally | Success | `ReadOutcome::Completed` |
| Stream ended | Success | `ReadOutcome::EndOfStream` |
| Timed read expired | Success | `ReadOutcome::TimedOut` |
| Non-blocking read found no input | Success | `ReadOutcome::WouldBlock` |
| Backend operation failed | Failure | The outcome reports the best available stopping state. |

If bytes or text are read before a terminating outcome, the result preserves the terminating outcome. For example, a line read that receives partial line text and then reaches end-of-stream returns that text with `ReadOutcome::EndOfStream`.

`wasTruncated = true` means the configured maximum byte count limited the returned text or line. Truncation is a successful result unless the backend also failed. Where practical, unread input remains available to later reads.

`LineReadResult::consumedLineEnding` reports which line ending ended a successful line read. `ReadLineEndingMode` controls whether the returned `line` strips, keeps, or normalizes the consumed line ending.
For line reads, `LineReadOptions::maxBytes` limits the returned line representation. If the line body fits but a kept or normalized line ending would exceed the limit, the line ending is consumed and reported, `wasTruncated` is set, and the returned line omits that ending instead of returning a partial line ending.

## Write Calls

Terminal writes use explicit operation names.

| Operation | Shape | Result |
| --- | --- | --- |
| Text write | `writeText("text")` | `IO::Types::Status` |
| Stream text write | `writeText(OutputStream::Stderr, "text")` | `IO::Types::Status` |
| Line write | `writeLine("text")` | `IO::Types::Status` |
| Stream line write | `writeLine(OutputStream::Stderr, "text")` | `IO::Types::Status` |
| Byte write | `writeBytes(std::span<const std::byte>{...})` | `IO::Types::WriteResult` |
| Stream byte write | `writeBytes(OutputStream::Stdout, std::span<const std::byte>{...})` | `IO::Types::WriteResult` |
| Segmented write | `writeSegments(std::span<const WriteSegment>{...})` | `IO::Types::Status` |
| Stream segmented write | `writeSegments(OutputStream::Stdout, std::span<const WriteSegment>{...})` | `IO::Types::Status` |
| Formatted text write | `print("value {}", value)` | `IO::Types::Status` |
| Formatted line write | `println("value {}", value)` | `IO::Types::Status` |

Text and segmented writes return `IO::Types::Status` because the operation may transform UTF-8 text into a platform-native console representation, emit styling, append line endings, reset style, and flush. Byte writes return `IO::Types::WriteResult` because callers need the number of accepted bytes.

`writeText()` writes text exactly as provided. Plain, unstyled text is passed directly to the backend without an assembly allocation. `writeLine()` assembles the text and configured line ending before one backend text write. `print()` and `println()` format with `std::format` semantics directly into reusable per-stream scratch storage.

Terminal serializes each stdout or stderr operation independently. It cannot serialize writes made through `std::cout`, `std::cerr`, `printf`, direct operating-system calls, or third-party output APIs.

## OutputBuffer

`OutputBuffer` batches plain UTF-8 text in memory before one terminal write. Use it in loops or code that naturally builds many small pieces:

```cpp
GameWIP::Terminal::Writer writer;
GameWIP::Terminal::OutputBuffer buffer(GameWIP::Terminal::Types::LineEnding::Lf);

buffer.reserve(4096);
buffer.print("entity {} ", id);
buffer.println("hp {}", hp);
buffer.flushTo(writer);
```

`OutputBuffer::writeTo()` writes without clearing. `OutputBuffer::flushTo()` clears only when the write succeeds.

## Explicit Empty Calls

Use the operation name that matches the empty value:

```cpp
GameWIP::Terminal::readLine();
GameWIP::Terminal::readText();
GameWIP::Terminal::writeText(std::string_view{});
GameWIP::Terminal::writeLine();
GameWIP::Terminal::writeBytes(std::span<const std::byte>{});
GameWIP::Terminal::writeSegments(std::span<const GameWIP::Terminal::Types::WriteSegment>{});
```

## Reader and Writer classes

`Reader` stores a default input stream for repeated reads and input-mode operations. Repeated reads use `readLine()`, `readText()`, and `readBytes()`.

`Writer` stores a default output stream for repeated writes and terminal controls. Repeated writes use `writeText()`, `writeLine()`, `writeBytes()`, `writeSegments()`, `print()`, and `println()`.

`Writer::prepareOutput()` prepares its default stream. The stream-taking overload can prepare either standard output stream without constructing another writer.

`Reader` and `Writer` are terminal convenience classes. They are not generic `GameWIP::IO` stream adapters and do not provide `operator>>` or `operator<<`.
