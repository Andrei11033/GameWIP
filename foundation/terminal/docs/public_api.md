@page terminal_public_api Public API

Include `terminal/terminal.h`. Installed consumers link `GameWIP::Terminal`; source-tree consumers link `Terminal`. Exact signatures and default arguments are in the generated reference; this page maps every public family and explains how the pieces fit together.

## Namespace layout

Active operations, factories, templates, `OutputBuffer`, and RAII scopes live in `GameWIP::Terminal`. Passive enums, options, capabilities, segments, and result structures live in `GameWIP::Terminal::Types`.

Most free operations have a default-stream overload and an explicit-stream overload. Default input means `InputStream::Stdin`; default output means `OutputStream::Stdout`.

## Constants

| Constant | Contract |
| --- | --- |
| `kWaitForever` | Negative timeout sentinel requesting an unbounded wait. Endpoint support still applies. |
| `kNoWait` | Zero timeout sentinel requesting a non-blocking attempt. |
| `kDefaultQueryTimeout` | Default best-effort timeout for protocol-style cursor queries. |
| `kDefaultMaxReturnedTextBytes` | Default maximum UTF-8 byte count returned by one text read. |
| `kDefaultMaxReturnedLineBytes` | Default maximum UTF-8 byte count returned by one line read. |

Timeout values are interpreted by the selected endpoint. A finite timeout can return `Unsupported` when the capability snapshot does not advertise timed reads.

## Factories

- `defaultColor()`, `basicColor()`, and `rgbColor()` create `Types::Color` values. An unknown `BasicColor` passed to `basicColor()` falls back to the terminal default color.
- `makeInputMode()` maps `InteractiveLine` or `RawBytes` to a portable `Types::InputMode`; an unknown preset falls back to `InteractiveLine`.
- `textSegment()`, `styledTextSegment()`, and `byteSegment()` create valid `Types::WriteSegment` values.

Text and byte segments retain non-owning views. Temporary owning strings and non-borrowed temporary byte ranges are rejected by deleted factory overloads so common dangling-view mistakes fail at compile time.

## Stream, outcome, and policy enums

| Type | Values |
| --- | --- |
| `InputStream` | `Stdin` |
| `OutputStream` | `Stdout`, `Stderr` |
| `StreamKind` | `Detached`, `Terminal`, `Redirected`, `Other` |
| `ReadOutcome` | `Completed`, `EndOfStream`, `TimedOut`, `WouldBlock` |
| `ConsumedLineEnding` | `None`, `Lf`, `CrLf`, `Cr` |
| `LineEnding` | `Native`, `Lf`, `CrLf` |
| `ReadLineEndingMode` | `Strip`, `Keep`, `NormalizeToLf` |
| `StyleMode` | `Never`, `Auto`, `Required` |
| `ColorKind` | `Default`, `Basic`, `Rgb` |
| `InputModePreset` | `InteractiveLine`, `RawBytes` |
| `CursorMoveDirection` | `Up`, `Down`, `Left`, `Right` |
| `ClearTarget` | `EntireScreen`, `ScreenBeforeCursor`, `ScreenAfterCursor`, `EntireScreenAndScrollback`, `EntireLine`, `LineBeforeCursor`, `LineAfterCursor` |
| `ScrollDirection` | `Up`, `Down` |
| `WriteSegmentKind` | `Text`, `StyledText`, `Bytes` |

`BasicColor` contains `Black`, `Red`, `Green`, `Yellow`, `Blue`, `Magenta`, `Cyan`, `White`, `BrightBlack`, `BrightRed`, `BrightGreen`, `BrightYellow`, `BrightBlue`, `BrightMagenta`, `BrightCyan`, and `BrightWhite`.

Unknown enum values crossing the public boundary return `InvalidArgument` unless a factory explicitly documents a fallback.

## Colors and styles

`Color` is valid by construction. Inspect `kind()` before using `basic()`, `red()`, `green()`, or `blue()`; accessors for a different color representation do not describe an active value.

`TextStyle` contains foreground and background colors plus `bold`, `dim`, `italic`, `underline`, `inverse`, and `strikethrough`. `StyleCapabilities` reports support for the same portable features. Capability reporting intentionally does not expose a backend protocol flag.

See @ref terminal_styling.

## Capabilities and geometry

`InputCapabilities` contains `kind`, `supportsUtf8Text`, `supportsByteInput`, `supportsLineInput`, `supportsRawInput`, `supportsEchoControl`, `supportsInputMode`, `supportsInputAvailability`, and `supportsReadTimeout`.

`OutputCapabilities` contains `kind`, `supportsUtf8Text`, `supportsByteOutput`, `supportsFlush`, `style`, `supportsTerminalSize`, `supportsCursorMovement`, `supportsCursorPositionQuery`, `supportsCursorSaveRestore`, `supportsCursorVisibility`, `supportsClear`, `supportsScroll`, `supportsAlternateScreen`, `supportsTitle`, and `supportsBell`.

Capability query results pair a status with the reported capability structure. `TerminalSize` contains `columns` and `rows`; `CursorPosition` contains zero-based `column` and `row`. Their result types pair the value with an IO status.

Capabilities are snapshots. Replacing or redirecting a standard handle can invalidate an earlier result.

## Options

| Type | Fields |
| --- | --- |
| `ByteReadOptions` | `timeout`, `allowPartial` |
| `TextReadOptions` | `timeout`, `maxReturnedBytes` |
| `LineReadOptions` | `timeout`, `maxReturnedBytes`, `lineEndingMode` |
| `TextWriteOptions` | `styleMode`, `style`, `flushMode` |
| `LineWriteOptions` | `styleMode`, `style`, `lineEnding`, `flushMode` |
| `ByteWriteOptions` | `flushMode` |
| `SegmentWriteOptions` | `styleMode`, `appendLineEnding`, `lineEnding`, `flushMode` |
| `ControlOptions` | `flushMode` |
| `CursorPositionQueryOptions` | `timeout`, `flushMode` |

A write or control flush request applies after emission. A flush failure can therefore accompany data that was already accepted by the endpoint.

## Read results

- `InputModeResult` contains `status` and `mode`.
- `InputAvailabilityResult` contains `status`, `available`, and a best-effort `estimatedBytes` value.
- `ByteReadResult` contains `status`, `outcome`, and `bytesRead`.
- `TextReadResult` contains `status`, `outcome`, UTF-8 `text`, and `wasTruncated`.
- `LineReadResult` contains `status`, `outcome`, UTF-8 `line`, `consumedLineEnding`, and `wasTruncated`.

`ByteReadResult::bytesRead` can preserve partial progress together with a terminating outcome or later failure. `LineReadResult::line` can preserve an unterminated line together with `EndOfStream`, `TimedOut`, or `WouldBlock`. A non-empty `TextReadResult::text` represents one completed UTF-8 chunk. Treat each structure as one result, not a collection of independent success flags. See @ref terminal_read_write.

## Input operations

- `getInputCapabilities()` and `getInputAvailability()` inspect stdin.
- `getInputMode()`, `setInputMode()`, and `restoreDefaultInputMode()` manage supported native input modes.
- `scopedInputMode()` applies a mode and returns an RAII restoration object.
- `readBytes()`, `readText()`, and `readLine()` provide raw-byte, UTF-8 chunk, and UTF-8 line reads.

Every family also has an explicit `InputStream` overload.

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

## RAII scopes

`InputModeScope`, `CursorHiddenScope`, and `AlternateScreenScope` are movable and non-copyable. A scope returned after failed setup is inactive and retains the setup status. Destructors make a best-effort non-throwing restore/leave attempt; call `restore()` or `leave()` explicitly when failure must be observed.

Move assignment first tries to restore the destination's currently owned state. If that restoration fails, the destination remains active and the source is not consumed.

## OutputBuffer

`OutputBuffer` owns reusable plain-text storage. It has ordinary copy/move value semantics and supports reserve, clear, size/query access, append operations, compile-time-checked formatting, and write-without-clear or write-and-clear-on-success operations.

`text()` returns a non-owning view into the buffer. Mutating, moving, or destroying the buffer can invalidate that view. The object is not internally synchronized for concurrent access. See @ref terminal_read_write.

## Exceptions

Expected terminal/backend failures use IO statuses and results, but the public API is not universally `noexcept`.

Free formatted output converts formatting and allocation failures from its formatting stage into statuses. `OutputBuffer` construction, reserve, append, and formatting retain normal standard-library exception behavior. Other operations may allocate temporary storage and can propagate exceptions not explicitly converted by their implementation. Scope factories are `noexcept` and store setup failure in the returned scope.

## Package boundary

Terminal is a shared library with a public IO dependency. See @ref terminal_abi for installed headers, exported template bridges, and binary-compatibility requirements.
