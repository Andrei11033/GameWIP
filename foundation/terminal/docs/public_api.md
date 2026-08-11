@page terminal_public_api Public API

Include `terminal/terminal.h`. Installed consumers link `GameWIP::Terminal`; source-tree consumers link `Terminal`. Exact signatures and default arguments are in the generated reference; this page maps every public family and explains how the pieces fit together.

## Namespace layout

Active direct operations, `Session`, factories, templates, `OutputBuffer`, and output-state RAII scopes live in `GameWIP::Terminal`. Passive enums, session/read options, structured events, capabilities, segments, and result structures live in `GameWIP::Terminal::Types`.

Most free operations have a default-stream overload and an explicit-stream overload. Default input means `InputStream::Stdin`; default output means `OutputStream::Stdout`. `Session` instead binds one input stream and one primary output stream for its complete open lifetime.

## Constants

| Constant | Contract |
| --- | --- |
| `kNoWait` | Readable `0ms` value requesting a non-blocking attempt. |
| `kDefaultQueryTimeout` | Default best-effort timeout for protocol-style cursor queries. |
| `kDefaultMaxReturnedTextBytes` | Default maximum UTF-8 byte count returned by one text read. |
| `kDefaultMaxReturnedLineBytes` | Default maximum UTF-8 byte count returned by one line read. |

Read options use `std::optional<std::chrono::milliseconds>` rather than a negative sentinel: `std::nullopt` waits indefinitely, zero polls, positive values establish one total operation deadline, and negative values are `InvalidArgument`. Endpoint support remains authoritative.

## Factories

- `defaultColor()`, `basicColor()`, and `rgbColor()` create `Types::Color` values. An unknown `BasicColor` passed to `basicColor()` falls back to the terminal default color.
- `textSegment()`, `styledTextSegment()`, and `byteSegment()` create valid `Types::WriteSegment` values.

Text and byte segments retain non-owning views. Temporary owning strings and non-borrowed temporary byte ranges are rejected by deleted factory overloads so common dangling-view mistakes fail at compile time.

## Stream, outcome, and policy enums

| Type | Values |
| --- | --- |
| `InputStream` | `Stdin` |
| `OutputStream` | `Stdout`, `Stderr` |
| `StreamKind` | `Detached`, `Terminal`, `Redirected`, `Other` |
| `InputDeliveryMode` | `Events`, `Stream` |
| `ControlKeyMode` | `NativeProcessing`, `ReportAsInput` |
| `ReadOutcome` | `Completed`, `EndOfStream`, `TimedOut`, `WouldBlock`, `Cancelled` |
| `ConsumedLineEnding` | `None`, `Lf`, `CrLf`, `Cr` |
| `LineEnding` | `Native`, `Lf`, `CrLf` |
| `ReadLineEndingMode` | `Strip`, `Keep`, `NormalizeToLf` |
| `StyleMode` | `Never`, `Auto`, `Required` |
| `ColorKind` | `Default`, `Basic`, `Rgb` |
| `CursorMoveDirection` | `Up`, `Down`, `Left`, `Right` |
| `ClearTarget` | `EntireScreen`, `ScreenBeforeCursor`, `ScreenAfterCursor`, `EntireScreenAndScrollback`, `EntireLine`, `LineBeforeCursor`, `LineAfterCursor` |
| `ScrollDirection` | `Up`, `Down` |
| `WriteSegmentKind` | `Text`, `StyledText`, `Bytes` |

`BasicColor` contains `Black`, `Red`, `Green`, `Yellow`, `Blue`, `Magenta`, `Cyan`, `White`, `BrightBlack`, `BrightRed`, `BrightGreen`, `BrightYellow`, `BrightBlue`, `BrightMagenta`, `BrightCyan`, and `BrightWhite`.

Unknown enum values crossing the public boundary return `InvalidArgument` unless a factory explicitly documents a fallback.

Structured input uses `Event` containing `KeyEvent`, `PasteEvent`, or `ResizeEvent`. `KeyEvent` carries a logical `Key`, modifier mask, action, location, and repeat count. `Key` can be a Unicode `CharacterKey`, portable `NamedKey`, numeric `FunctionKey`, standalone `ModifierKey`, or `MediaKey`. Richer key details are capability-gated; no Win32 virtual-key/scancode/native handle type crosses the public boundary.

## Colors and styles

`Color` is valid by construction. Inspect `kind()` before using `basic()`, `red()`, `green()`, or `blue()`; accessors for a different color representation do not describe an active value.

`TextStyle` contains foreground and background colors plus `bold`, `dim`, `italic`, `underline`, `inverse`, and `strikethrough`. `StyleCapabilities` reports support for the same portable features. Capability reporting intentionally does not expose a backend protocol flag.

See @ref terminal_styling.

## Capabilities and geometry

`InputCapabilities` describes the managed abstraction through `kind`, `supportsUtf8Text`, `supportsByteInput`, `supportsLineInput`, `supportsEventInput`, `supportsNonBlockingReads`, `supportsFiniteTimeouts`, `supportsCancellation`, `supportsResizeEvents`, `supportsPasteEvents`, `supportsKeyRepeatEvents`, `supportsKeyReleaseEvents`, `supportsStandaloneModifierEvents`, `supportsMediaKeyEvents`, `supportsKeyLocation`, and `supportsModifierState`.

`OutputCapabilities` contains `kind`, `supportsUtf8Text`, `supportsByteOutput`, `supportsFlush`, `style`, `supportsTerminalSize`, `supportsCursorMovement`, `supportsCursorPositionQuery`, `supportsCursorSaveRestore`, `supportsCursorVisibility`, `supportsClear`, `supportsScroll`, `supportsAlternateScreen`, `supportsTitle`, and `supportsBell`.

Capability query results pair a status with the reported capability structure. `TerminalSize` contains `columns` and `rows`; `CursorPosition` contains zero-based `column` and `row`. Their result types pair the value with an IO status.

Capabilities are snapshots. Replacing or redirecting a standard handle can invalidate an earlier result.

## Options

| Type | Fields |
| --- | --- |
| `SessionOptions` | `input`, `output`, `deliveryMode`, `controlKeyMode` |
| `EventReadOptions` | `timeout`, `stopToken` |
| `ByteReadOptions` | `timeout`, `stopToken`, `allowPartial` |
| `TextReadOptions` | `timeout`, `stopToken`, `maxReturnedBytes` |
| `LineReadOptions` | `timeout`, `stopToken`, `maxReturnedBytes`, `echo`, `lineEndingMode` |
| `TextWriteOptions` | `styleMode`, `style`, `flushMode` |
| `LineWriteOptions` | `styleMode`, `style`, `lineEnding`, `flushMode` |
| `ByteWriteOptions` | `flushMode` |
| `SegmentWriteOptions` | `styleMode`, `appendLineEnding`, `lineEnding`, `flushMode` |
| `ControlOptions` | `flushMode` |
| `CursorPositionQueryOptions` | `timeout`, `flushMode` |

A write or control flush request applies after emission. A flush failure can therefore accompany data that was already accepted by the endpoint.

## Read results

- `EventReadResult` contains `status`, `outcome`, and an optional structured `event`.
- `ByteReadResult` contains `status`, `outcome`, and `bytesRead`.
- `TextReadResult` contains `status`, `outcome`, UTF-8 `text`, and `wasTruncated`.
- `LineReadResult` contains `status`, `outcome`, UTF-8 `line`, `consumedLineEnding`, and `wasTruncated`.

`ByteReadResult::bytesRead` can preserve partial progress together with a terminating outcome or later failure. `LineReadResult::line` can preserve an unterminated line together with `EndOfStream`, `TimedOut`, `WouldBlock`, or `Cancelled`. A non-empty `TextReadResult::text` represents completed valid UTF-8. Treat each structure as one result, not a collection of independent success flags. See @ref terminal_read_write.

## Input operations

- `getInputCapabilities()` observes the current managed stdin capability snapshot.
- `readEvent()` performs a temporary managed event read.
- `readBytes()`, `readText()`, and `readLine()` perform temporary managed Stream-delivery reads.
- Direct reads acquire exclusive stdin ownership, configure required terminal state, perform the operation, restore exact captured state, and release ownership before returning.

Every direct read family also has an explicit `InputStream` overload. Availability-check-then-read and public native-mode mutation are intentionally absent; use a zero-duration read for polling and `Session` for persistent ownership.

On an interactive terminal, both delivery modes use the same immediate native event engine. `readText()` and `readBytes()` derive stream data from logical character/control events, while `readLine()` adds Terminal-owned Unicode line discipline. `LineReadOptions::echo` controls managed rendering; redirected input keeps byte/text/line semantics and ignores the echo option.

## Session

`Session` is closed by default, move-constructible, non-copyable, and deliberately not move-assignable. `open()` binds `SessionOptions`, claims exclusive managed input ownership, caches input capabilities, and configures required native terminal state. `isOpen()` reports ownership state. `close()` restores the exact captured native input state before releasing ownership.

Opening an already-open object returns `AlreadyOpen`. Another session or direct read competing for the same input returns `ResourceBusy`. Explicit close restoration failure leaves the session open and ownership retained so the caller can retry; destruction makes a best-effort non-throwing restoration attempt and cannot surface cleanup failure.

`Session::readEvent()` requires `InputDeliveryMode::Events`. `Session::readBytes()`, `readText()`, and `readLine()` require `InputDeliveryMode::Stream`. Incompatible operations return `Unsupported` without consuming unrelated input. One session operation is serialized against `close()` on the same object.

Interactive Stream sessions are not a fallback to native cooked input: Terminal disables native line buffering/echo, retains structured decoder state, and implements Backspace/Delete, grapheme-aware left/right movement, Home/End, Enter completion, bounded paste insertion, optional echo, deadlines, and cancellation itself.

## Output operations

- `getOutputCapabilities()` observes active support without modifying the endpoint.
- `prepareOutput()` enables support required by styling and controls when the backend can do so.
- `getTerminalSize()` queries character-cell dimensions.
- `writeText()` and `writeLine()` write UTF-8 text.
- `writeBytes()` writes arbitrary bytes where the endpoint supports byte output.
- `writeSegments()` emits one mixed logical batch.
- `print()` and `println()` provide compile-time-checked `std::format` syntax.
- `flush()` applies an IO flush mode to the selected output endpoint.

Every family has a stdout default and an explicit `OutputStream` overload. See @ref terminal_read_write, @ref terminal_segmented_writes, and @ref terminal_capabilities_and_redirection.

## Control operations

Terminal provides `resetStyle()`, `moveCursor()`, `setCursorPosition()`, `getCursorPosition()`, `saveCursorPosition()`, `restoreCursorPosition()`, `setCursorVisible()`, `clear()`, `scroll()`, `enterAlternateScreen()`, `leaveAlternateScreen()`, `setTitle()`, and `ringBell()`.

`scopedCursorHidden()` and `scopedAlternateScreen()` provide nesting-aware temporary state. Controls require endpoint support and are not portable substitutes for a complete terminal UI framework. See @ref terminal_control_primitives.

## Output-state RAII scopes

`CursorHiddenScope` and `AlternateScreenScope` remain movable and non-copyable helpers for temporary output control state. A scope returned after failed setup is inactive and retains the setup status. Destructors make a best-effort non-throwing restore/leave attempt; call `restore()` or `leave()` explicitly when failure must be observed.

Managed input lifetime is not represented by a scope factory anymore; use `Session::open()` / `close()` so ownership, delivery mode, cancellation, and exact native restoration share one contract.

## OutputBuffer

`OutputBuffer` owns reusable plain-text storage. It has ordinary copy/move value semantics and supports reserve, clear, size/query access, append operations, compile-time-checked formatting, and write-without-clear or write-and-clear-on-success operations.

`text()` returns a non-owning view into the buffer. Mutating, moving, or destroying the buffer can invalidate that view. The object is not internally synchronized for concurrent access. See @ref terminal_read_write.

## Exceptions

Expected terminal/backend failures use IO statuses and results, but the public API is not universally `noexcept`.

Free formatted output converts formatting and allocation failures from its formatting stage into statuses. `OutputBuffer` construction, reserve, append, and formatting retain normal standard-library exception behavior in this intermediate #57 slice. `Session` lifecycle and managed read entry points are `noexcept` and translate owned allocation/setup/backend exceptions to statuses. Output-state scope factories remain `noexcept` and store setup failure in the returned scope.

## Package boundary

Terminal is a shared library with a public IO dependency. See @ref terminal_abi for installed headers, exported template bridges, and binary-compatibility requirements.
