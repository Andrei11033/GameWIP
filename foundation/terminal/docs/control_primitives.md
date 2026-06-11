@page terminal_control_primitives Terminal control primitives

Primitive controls are implemented for Windows real console output streams when the backend can enable virtual terminal processing. Detached, redirected, or unsupported streams return explicit status failures for controls that cannot run.

## Scope

Terminal controls are low-level terminal primitives. They exist because higher-level tools need a stable foundation for basic terminal state changes.

These controls do not make Terminal a widget, layout, prompt, menu, progress-bar, table, mouse, keyboard-event, terminal-session, or command framework.

## Cursor position queries

`getCursorPosition(options)` uses stdout and stdin. `getCursorPosition(outputStream, responseStream, options)` selects both protocol endpoints explicitly. Queries may send a request to the output stream and consume a response from the selected input stream. Implementations serialize both endpoints and return `Unsupported` when a reliable query is unavailable. The Windows backend uses `GetConsoleScreenBufferInfo` for real console streams instead of consuming input, so `CursorPositionQueryOptions::timeout` does not cause a wait on Win32 real-console streams.

Absolute cursor positions are zero-based in the public API even when a backend protocol uses one-based coordinates.

The active backend validates control parameters before output. The Win32 VT backend limits movement and scroll parameters to 32767, zero-based absolute coordinates to 32766, and titles to 254 UTF-8 bytes. Other backends may expose different limits without changing the public types.

## Temporary state

`CursorHiddenScope` is an RAII helper for temporary hidden-cursor states. Nested scopes on the same output stream emit one hide on the outer transition and one show after the final scope restores. Its destructor must not throw. Failed explicit restoration remains active and may be retried.

`AlternateScreenScope` is nesting-safe per output stream and leaves alternate screen mode after the final active scope. Its destructor must not throw. Failed explicit leave remains active and may be retried.

Restore or leave nested scopes in reverse acquisition order. Use the manual visibility and alternate-screen functions when the state must outlive a lexical scope.

Do not mix manual visibility or alternate-screen transitions with active scopes for the same stream.

## Title and bell

Title output removes embedded escape and bell bytes so caller text cannot terminate the control sequence early.

## Flush behavior

Terminal controls use `Types::ControlOptions`, which currently carries `IO::Types::FlushMode`.

Unsupported controls return `IO::Types::ErrorCode::Unsupported`. Expected backend failures are reported through `IO::Types::Status`.

Unknown flush modes return `InvalidArgument` before a control sequence is written. On Win32, a requested flush reaches `FlushFileBuffers` only for output redirected to a regular disk file; console and pipe output have no Terminal-owned pending buffer and treat the request as a successful no-op.

A failed control write may already have emitted a prefix of its encoded sequence. Control statuses report completion, not transactional rollback.
