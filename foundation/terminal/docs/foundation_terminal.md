@page foundation_terminal Terminal

`GameWIP::Terminal` is the foundation terminal primitive library.

Terminal covers stdin reads, stdout/stderr writes, styling, capability queries, input modes, and primitive controls on the current Windows backend. Deterministic validation builds can enable internal test hooks.

## Documentation sections

- @subpage foundation_terminal_read_write
- @subpage foundation_terminal_styling
- @subpage foundation_terminal_segmented_writes
- @subpage foundation_terminal_control_primitives
- @subpage foundation_terminal_capabilities_and_redirection
- @subpage foundation_terminal_input_modes
- @subpage foundation_terminal_unicode_terminal_io
- @subpage foundation_terminal_testing
- @subpage foundation_terminal_color_and_redirection
- @subpage foundation_terminal_unicode_terminal_output

## Purpose

`GameWIP::Terminal` hides platform-specific terminal behavior behind a stable public API.

Terminal owns primitive terminal I/O and controls:

- stdin reading;
- stdout and stderr writing;
- UTF-8 terminal text I/O;
- byte I/O where practical;
- line reads;
- input mode control;
- terminal capability detection;
- terminal size and cursor position queries;
- text style and color output;
- optimized segmented writes;
- cursor movement, save/restore, visibility, clearing, scrolling, alternate screen, title, bell, and flushing;
- RAII scopes for temporary terminal states that must be restored.

Terminal is not a widget, prompt, menu, layout, progress-bar, table, TUI, terminal-session, or event framework. Those higher-level behaviors may build on Terminal later, but they are not part of this foundation library.

Terminal does not own Windows popups, Assert dialogs, debugger output, filesystem behavior, networking, config parsing, asset loading, logging policy, or async I/O.

## Dependency

`GameWIP::Terminal` depends on `GameWIP::IO`.

Terminal uses `IO::Types::Status`, `IO::Types::WriteResult`, and `IO::Types::FlushMode` where those shared IO concepts fit. Terminal does not make `GameWIP::IO` responsible for operating-system terminal handles or platform behavior.

## Namespace

```cpp
namespace GameWIP::Terminal;
namespace GameWIP::Terminal::Types;
```

Passive data shapes live under `GameWIP::Terminal::Types`. Active APIs live directly under `GameWIP::Terminal`.

## API family map

| Family | Public APIs | Primary behavior |
| --- | --- | --- |
| Helpers | `defaultColor`, `basicColor`, `rgbColor`, `makeInputMode`, `textSegment`, `styledSegment`, `byteSegment` | Create small passive Terminal values. |
| Capabilities | `getInputCapabilities`, `getOutputCapabilities`, `Reader::getCapabilities`, `Writer::getCapabilities` | Detect stream kind and supported input/output features. |
| Input | `readLine`, `readText`, `readBytes`, `Reader`, `getInputAvailability`, `getInputMode`, `setInputMode`, `restoreDefaultInputMode`, `InputModeScope` | Read stdin data and control input behavior. |
| Output | `writeText`, `writeLine`, `writeBytes`, `writeSegments`, `print`, `println`, `OutputBuffer`, `Writer`, `flush`, `resetStyle` | Write text, formatted text, bytes, and segments to stdout or stderr. |
| Controls | `getTerminalSize`, `moveCursor`, `setCursorPosition`, `getCursorPosition`, `saveCursorPosition`, `restoreCursorPosition`, `setCursorVisible`, `clear`, `scroll`, `enterAlternateScreen`, `leaveAlternateScreen`, `setTitle`, `ringBell` | Expose primitive terminal controls. |
| RAII scopes | `InputModeScope`, `AlternateScreenScope`, `CursorVisibilityScope` | Restore dangerous temporary terminal states without throwing from destructors. |

## Public names

Terminal uses explicit public operation names instead of broad `read(...)` and `write(...)` overload sets. The read side has `readLine()`, `readText()`, and `readBytes()`. The write side has `writeText()`, `writeLine()`, `writeBytes()`, `writeSegments()`, `print()`, and `println()`.

No `operator<<` or `operator>>` overloads are provided. Terminal calls should stay explicit about stream, result, options, and failure behavior.

`readLine()` with no arguments reads one line from stdin. `writeText()` with no stream writes UTF-8 text to stdout. `writeLine()` appends a line ending. `print()` and `println()` format with `std::format` semantics before writing. Stderr is selected explicitly with `Types::OutputStream::Stderr` or by constructing a `Writer` for stderr.

## Reader and Writer

`Reader` is the convenience object for repeated terminal input operations. It has a default input stream, defaulting to `Types::InputStream::Stdin`.

`Writer` is the convenience object for repeated terminal output and control operations. It has a default output stream, defaulting to `Types::OutputStream::Stdout`. Construct another `Writer` or use the free stream-explicit functions when targeting stderr.

Terminal `Reader` and `Writer` do not inherit from `IO::Reader` or `IO::Writer`. The IO classes are generic byte stream contracts with open, close, seek, and whole-stream helper behavior. Terminal objects expose terminal-specific input modes, read outcomes, styling, control operations, stdout/stderr routing, and capability detection.

## Option types

Terminal keeps operation-specific option structs instead of one generic `ReadOptions` and one generic `WriteOptions`.

This is intentional:

- byte reads use a caller buffer and `allowPartial`; text and line read options intentionally do not expose `allowPartial`. Text reads return one available UTF-8 chunk, while line reads wait for a line ending, terminating outcome, truncation, or failure.
- text writes use style and flush options; line writes use style, line-ending, and flush options; byte writes only use flush behavior; segmented writes use per-segment style plus batch line-ending behavior; controls use flush behavior.

A single catch-all options struct would either need fields that many calls ignore or would require adding another selector enum. The operation-specific structs keep overload resolution and contracts clearer.

## Implementation state

`foundation/terminal/terminal.h` is the public declaration contract. Runtime behavior is implemented through `foundation/terminal/core/terminal.cpp` and the active platform backend.

Backend details belong in internal backend contract documentation and platform source files, not in the public API.
