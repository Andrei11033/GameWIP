@page terminal_control_primitives Terminal control primitives

This page documents primitive terminal controls.

Primitive controls are implemented for Windows real console output streams when the backend can enable virtual terminal processing. Detached, redirected, or unsupported streams return explicit status failures for controls that cannot run.

## Scope

Terminal controls are low-level terminal primitives. They exist because higher-level tools need a stable foundation for basic terminal state changes.

These controls do not make Terminal a widget, layout, prompt, menu, progress-bar, table, mouse, keyboard-event, terminal-session, or command framework.

## Cursor movement and position

`moveCursor(direction, amount, options)` moves the cursor relative to its current position.

`setCursorPosition(position, options)` requests an absolute zero-based cursor position.

`getCursorPosition(options)` queries the current cursor position when supported. Cursor position queries may send a control request to the output stream and consume a response from the corresponding input stream. Implementations must serialize this interaction with Terminal input operations and return `Unsupported` when a reliable query is unavailable. The Windows backend uses `GetConsoleScreenBufferInfo` for real console streams instead of consuming stdin, so `CursorPositionQueryOptions::timeout` does not cause a wait on Win32 real-console streams.

`saveCursorPosition(options)` and `restoreCursorPosition(options)` expose primitive save/restore cursor behavior when supported by the backend.

## Cursor visibility

`setCursorVisible(visible, options)` changes cursor visibility where supported.

`CursorVisibilityScope` is an RAII helper for temporary hidden-cursor states. Its destructor must not throw. Callers that need to observe restore failure must call `restore()` explicitly before destruction.

## Clearing

`clear(target, options)` supports:

- entire visible screen;
- screen before cursor;
- screen after cursor;
- entire screen and scrollback where supported;
- entire current line;
- line before cursor;
- line after cursor.

## Scrolling

`scroll(direction, lines, options)` exposes primitive terminal scrolling when supported.

## Alternate screen

`enterAlternateScreen(options)` and `leaveAlternateScreen(options)` control alternate screen mode.

`AlternateScreenScope` leaves alternate screen mode when destroyed. Its destructor must not throw. Callers that need to observe leave failure must call `leave()` explicitly before destruction.

## Title and bell

`setTitle(utf8Title, options)` requests a terminal title change where supported.

`ringBell(options)` requests terminal bell output where supported.

## Flush behavior

Terminal controls use `Types::ControlOptions`, which currently carries `IO::Types::FlushMode`.

Unsupported controls return `IO::Types::ErrorCode::Unsupported`. Expected backend failures are reported through `IO::Types::Status`.
