@page terminal_public_api Public API

Include `terminal/terminal.h` for the normal umbrella API. Code that only needs portable styling values/factories may include the independently
supported `terminal/style.h`. Installed consumers link `GameWIP::Terminal`; source-tree consumers link `Terminal`. Exact signatures and default
arguments are in the generated reference; this page maps every public family and explains how the pieces fit together.

## Namespace layout

Active direct operations, `Session`, factories, templates, `OutputBuffer`, and output-state RAII scopes live in `GameWIP::Terminal`. Passive public
types remain under one `GameWIP::Terminal::Types` root and are organized by real domains: `Types::Input`, `Types::Output`, `Types::Events`,
`Types::Style`, and `Types::Cursor`; genuinely shared concepts such as `Types::StreamKind`, `Types::SessionOptions`, `Types::Size`, and `Types::Event`
stay directly under `Types`.

Most free operations have a default-stream overload and an explicit-stream overload. Default input means `Types::Input::Stream::Stdin`; default output
means `Types::Output::Stream::Stdout`. `Session` instead binds one input stream and one primary output stream for its complete open lifetime.

## Constants

| Constant | Contract |
| --- | --- |
| `kNoWait` | Readable `0ms` value requesting a non-blocking attempt. |
| `kDefaultQueryTimeout` | Default best-effort timeout for protocol-style cursor queries. |
| `kDefaultMaxReturnedTextBytes` | Default maximum UTF-8 byte count returned by one text read. |
| `kDefaultMaxReturnedLineBytes` | Default maximum UTF-8 byte count returned by one line read. |

Read options use `std::optional<std::chrono::milliseconds>` rather than a negative sentinel: `std::nullopt` waits indefinitely, zero polls, positive
values establish one total operation deadline, and negative values are `InvalidArgument`. Endpoint support remains authoritative.

## Factories

- `defaultColor()`, `basicColor()`, and `rgbColor()` create `Types::Style::Color` values. An unknown `Types::Style::BasicColor` passed to
  `basicColor()` falls back to the terminal default color.
- `textSegment()`, `styledTextSegment()`, and `byteSegment()` create valid `Types::Output::Segment` values.

Text and byte segments retain non-owning views. Temporary owning strings and non-borrowed temporary byte ranges are rejected by deleted factory
overloads so common dangling-view mistakes fail at compile time.

## Stream, outcome, and policy enums

| Type | Values |
| --- | --- |
| `Types::Input::Stream` | `Stdin` |
| `Types::Output::Stream` | `Stdout`, `Stderr` |
| `StreamKind` | `Detached`, `Terminal`, `Redirected`, `Other` |
| `Types::Input::DeliveryMode` | `Events`, `Stream` |
| `Types::Input::ControlKeyMode` | `NativeProcessing`, `ReportAsInput` |
| `Types::Input::ReadOutcome` | `Completed`, `EndOfStream`, `TimedOut`, `WouldBlock`, `Cancelled` |
| `Types::Input::ConsumedLineEnding` | `None`, `Lf`, `CrLf`, `Cr` |
| `Types::Output::LineEnding` | `Native`, `Lf`, `CrLf` |
| `Types::Input::LineEndingMode` | `Strip`, `Keep`, `NormalizeToLf` |
| `Types::Style::Mode` | `Never`, `Auto`, `Required` |
| `Types::Style::ColorKind` | `Default`, `Basic`, `Rgb` |
| `Types::Cursor::MoveDirection` | `Up`, `Down`, `Left`, `Right` |
| `Types::Output::ClearTarget` | `EntireScreen`, `ScreenBeforeCursor`, `ScreenAfterCursor`, `EntireScreenAndScrollback`, `EntireLine`, `LineBeforeCursor`, `LineAfterCursor` |
| `Types::Output::ScrollDirection` | `Up`, `Down` |
| `Types::Output::SegmentKind` | `Text`, `StyledText`, `Bytes` |

`Types::Style::BasicColor` contains `Black`, `Red`, `Green`, `Yellow`, `Blue`, `Magenta`, `Cyan`, `White`, `BrightBlack`, `BrightRed`, `BrightGreen`,
`BrightYellow`, `BrightBlue`, `BrightMagenta`, `BrightCyan`, and `BrightWhite`.

Unknown enum values crossing the public boundary return `InvalidArgument` unless a factory explicitly documents a fallback.

Structured input uses `Event` containing `Types::Events::Key`, `Types::Events::Paste`, or `Types::Events::Resize`. `Types::Events::Key` carries a
logical `Types::Events::KeyValue`, modifier mask, action, location, and repeat count. `Types::Events::KeyValue` can be a Unicode
`Types::Events::CharacterKey`, portable `Types::Events::NamedKey`, numeric `Types::Events::FunctionKey`, standalone `Types::Events::ModifierKey`, or
`Types::Events::MediaKey`. Richer key details are capability-gated; no Win32 virtual-key/scancode/native handle type crosses the public boundary.

## Colors and styles

`Types::Style::Color` is valid by construction. Inspect `kind()` before using `basic()`, `red()`, `green()`, or `blue()`; accessors for a different
color representation do not describe an active value.

`Types::Style::Request` contains foreground and background colors plus `bold`, `dim`, `italic`, `underline`, `inverse`, and `strikethrough`.
`Types::Style::Capabilities` reports support for the same portable features. Capability reporting intentionally does not expose a backend protocol
flag.

See @ref terminal_styling.

## Capabilities and geometry

`Types::Input::Capabilities` describes the current managed input abstraction:

- Endpoint kind and UTF-8, byte, line, or structured-event input.
- Non-blocking reads, finite deadlines, and cancellation.
- Resize and paste events.
- Key repeat/release, standalone modifiers, media keys, key location, and
  modifier-state detail.

`Types::Output::Capabilities` describes:

- Endpoint kind and UTF-8 or byte output.
- Flush and portable style support.
- Terminal geometry and cursor movement, query, save/restore, and visibility.
- Clear, scroll, alternate-screen, title, and bell controls.

The generated `Capabilities` field reference gives the exact `supports...`
member associated with each item.

Capability query results pair a status with the reported capability structure. `Types::Size` contains `columns` and `rows`; `Types::Cursor::Position`
contains zero-based `column` and `row`. Their result types pair the value with an IO status.

Capabilities are snapshots. Replacing or redirecting a standard handle can invalidate an earlier result.

## Options

| Type | Fields |
| --- | --- |
| `SessionOptions` | `input`, `output`, `deliveryMode`, `controlKeyMode` |
| `Types::Input::EventOptions` | `timeout`, `stopToken` |
| `Types::Input::ByteOptions` | `timeout`, `stopToken`, `allowPartial` |
| `Types::Input::TextOptions` | `timeout`, `stopToken`, `maxReturnedBytes` |
| `Types::Input::LineOptions` | `timeout`, `stopToken`, `maxReturnedBytes`, `echo`, `lineEndingMode` |
| `Types::Output::TextOptions` | `styleMode`, `style`, `flushMode` |
| `Types::Output::LineOptions` | `styleMode`, `style`, `lineEnding`, `flushMode` |
| `Types::Output::ByteOptions` | `flushMode` |
| `Types::Output::SegmentOptions` | `styleMode`, `appendLineEnding`, `lineEnding`, `flushMode` |
| `Types::Output::ControlOptions` | `flushMode` |
| `Types::Cursor::QueryOptions` | `timeout`, `flushMode` |

A write or control flush request applies after emission. A flush failure can therefore accompany data that was already accepted by the endpoint.

## Read results

- `Types::Input::EventResult` contains `status`, `outcome`, and an optional structured `event`.
- `Types::Input::ByteResult` contains `status`, `outcome`, and `bytesRead`.
- `Types::Input::TextResult` contains `status`, `outcome`, UTF-8 `text`, and `wasTruncated`.
- `Types::Input::LineResult` contains `status`, `outcome`, UTF-8 `line`, `consumedLineEnding`, and `wasTruncated`.

`Types::Input::ByteResult::bytesRead` can preserve partial progress together with a terminating outcome or later failure.
`Types::Input::LineResult::line` can preserve an unterminated line together with `EndOfStream`, `TimedOut`, `WouldBlock`, or `Cancelled`. A non-empty
`Types::Input::TextResult::text` represents completed valid UTF-8. Treat each structure as one result, not a collection of independent success flags.
See @ref terminal_read_write.

## Input operations

- `getInputCapabilities()` observes the current managed stdin capability snapshot.
- `readEvent()` performs a temporary managed event read.
- `readBytes()`, `readText()`, and `readLine()` perform temporary managed Stream-delivery reads.
- Direct reads acquire exclusive stdin ownership, configure required terminal state, perform the operation, restore exact captured state, and release
  ownership before returning.

Every direct read family also has an explicit `Types::Input::Stream` overload. Availability-check-then-read and public native-mode mutation are
intentionally absent; use a zero-duration read for polling and `Session` for persistent ownership.

On an interactive terminal, both delivery modes use the same immediate native event engine. `readText()` and `readBytes()` derive stream data from
logical character/control events, while `readLine()` adds Terminal-owned Unicode line discipline. `Types::Input::LineOptions::echo` controls managed
rendering; redirected input keeps byte/text/line semantics and ignores the echo option.

## Session

`Session` is closed by default, move-constructible, non-copyable, and deliberately not move-assignable. `open()` binds `SessionOptions`, claims
exclusive managed input ownership, caches input capabilities, and configures required native terminal state. `isOpen()` reports ownership state.
`close()` restores Session-owned persistent output state in reverse activation order and then restores the exact captured native input state before
releasing ownership.

Opening an already-open object returns `AlreadyOpen`. Another session or direct read competing for the same input returns `ResourceBusy`. Explicit
close restoration failure leaves the session open and ownership retained so the caller can retry; destruction makes a best-effort non-throwing
restoration attempt and cannot surface cleanup failure.

`Session::readEvent()` requires `Types::Input::DeliveryMode::Events`.
`readBytes()`, `readText()`, and `readLine()` require `DeliveryMode::Stream`.
An incompatible operation returns `Unsupported` without consuming input.

Input-consuming Session calls serialize with each other. Output continues to
use the process-wide coordinator for its bound stream, so another thread may
write while a Session read blocks. `close()` stops new unrelated operations and
waits for active calls before restoring state.

Formatting may reenter global Terminal output or the same Session. Calling
`close()` on that Session from inside its active formatter returns
`ResourceBusy` instead of waiting on itself.

The bound output surface mirrors direct output through `Session::getOutputCapabilities()`, `prepareOutput()`, size/cursor queries,
text/line/byte/segment writes, formatting, flush, styling, cursor controls, clear/scroll, alternate screen, title, and bell. Those methods delegate to
the same global output implementation rather than duplicating backend logic. Session-owned cursor hiding and alternate-screen entry are tracked for
reverse-order cleanup; opening a Session does not change either state automatically.

Interactive Stream sessions are not a fallback to native cooked input: Terminal disables native line buffering/echo, retains structured decoder state,
and implements Backspace/Delete, grapheme-aware left/right movement, Home/End, Enter completion, bounded paste insertion, optional echo, deadlines,
and cancellation itself.

## Output operations

- `getOutputCapabilities()` observes active support without modifying the endpoint.
- `prepareOutput()` enables support required by styling and controls when the backend can do so.
- `getTerminalSize()` queries character-cell dimensions.
- `writeText()` and `writeLine()` write UTF-8 text.
- `writeBytes()` writes arbitrary bytes where the endpoint supports byte output.
- `writeSegments()` emits one mixed logical batch.
- `print()` and `println()` provide compile-time-checked `std::format` syntax.
- `flush()` applies an IO flush mode to the selected output endpoint.

Every family has a stdout default and an explicit `Types::Output::Stream` overload. See @ref terminal_read_write, @ref terminal_segmented_writes, and
@ref terminal_capabilities_and_redirection.

## Control operations

Terminal provides `resetStyle()`, `moveCursor()`, `setCursorPosition()`, `getCursorPosition()`, `saveCursorPosition()`, `restoreCursorPosition()`,
`setCursorVisible()`, `clear()`, `scroll()`, `enterAlternateScreen()`, `leaveAlternateScreen()`, `setTitle()`, and `ringBell()`.

`scopedCursorHidden()` and `scopedAlternateScreen()` provide nesting-aware temporary state. Controls require endpoint support and are not portable
substitutes for a complete terminal UI framework. See @ref terminal_control_primitives.

## Output-state RAII scopes

`CursorHiddenScope` and `AlternateScreenScope` remain movable and non-copyable helpers for temporary output control state. A setup failure before
control-sequence emission returns an inactive scope. A requested flush failure after successful emission is also retained in `status()`, but the scope
stays active and owns cleanup. Destructors make a best-effort non-throwing restore/leave attempt; call `restore()` or `leave()` explicitly when
failure must be observed.

Managed input lifetime is not represented by a scope factory anymore; use `Session::open()` / `close()` so ownership, delivery mode, cancellation, and
exact native restoration share one contract.

## OutputBuffer

`OutputBuffer` owns reusable plain-text storage. It defaults safely to the native line ending; `setLineEnding()` is the checked way to change that
policy. `reserve()`, `appendText()`, `appendLine()`, `print()`, and `println()` are status-returning `noexcept` mutations. Formatting writes directly
into retained caller-owned storage and rolls back to the previous size if formatting or allocation fails, avoiding a second permanent scratch buffer
and preventing partial formatted records.

`writeTo()` preserves the buffer. `flushTo()` writes and clears only after a successful write, preserving retry data on failure. `clear()` retains
capacity. `text()` returns a non-owning view into the buffer; mutating, moving, or destroying the buffer can invalidate that view. The object is not
internally synchronized for concurrent access. See @ref terminal_read_write.

## Exceptions

Checked Terminal operations contain failures caused by Terminal-owned allocation, formatting, conversion, and backend work and represent them with IO
statuses/results. Checked direct operations, Session lifecycle/input/output/formatting/query/control operations, and `OutputBuffer` allocating
mutations/formatting are `noexcept`.

Caller-side construction of owning `std::string`, custom formatter arguments, or other allocating arguments before Terminal receives control is
outside this guarantee. Common code-only failure statuses remain allocation-free; diagnostic enrichment is best effort and never replaces the primary
failure.

## Package boundary

Terminal is a shared library with a public IO dependency. See @ref terminal_abi for installed headers, exported template bridges, and
binary-compatibility requirements.
