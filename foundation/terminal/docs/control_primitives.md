@page terminal_control_primitives Terminal control primitives

Primitive controls are implemented for Windows real console output streams when the backend can enable virtual terminal processing. Detached, redirected, or unsupported streams return explicit status failures for controls that cannot run.

## Scope

Terminal controls are low-level terminal primitives. They exist because higher-level tools need a stable foundation for basic terminal state changes.

These controls do not make Terminal a widget, layout, prompt, menu, progress-bar, table, mouse, keyboard-event, terminal-session, or command framework.

## Cursor position queries

`getCursorPosition(options)` queries the current cursor position when supported. Cursor position queries may send a control request to the output stream and consume a response from the corresponding input stream. Implementations must serialize this interaction with Terminal input operations and return `Unsupported` when a reliable query is unavailable. The Windows backend uses `GetConsoleScreenBufferInfo` for real console streams instead of consuming stdin, so `CursorPositionQueryOptions::timeout` does not cause a wait on Win32 real-console streams.

Absolute cursor positions are zero-based in the public API even when a backend protocol uses one-based coordinates.

## Temporary state

`CursorVisibilityScope` is an RAII helper for temporary hidden-cursor states. Its destructor must not throw. Callers that need to observe restore failure must call `restore()` explicitly before destruction.

`AlternateScreenScope` leaves alternate screen mode when destroyed. Its destructor must not throw. Callers that need to observe leave failure must call `leave()` explicitly before destruction.

## Title and bell

Title output removes embedded escape and bell bytes so caller text cannot terminate the control sequence early.

## Flush behavior

Terminal controls use `Types::ControlOptions`, which currently carries `IO::Types::FlushMode`.

Unsupported controls return `IO::Types::ErrorCode::Unsupported`. Expected backend failures are reported through `IO::Types::Status`.
