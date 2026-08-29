@page terminal_input_modes Sessions and managed input ownership

Terminal no longer exposes public native input-mode mutation. `Session` is the public persistent ownership/configuration boundary; direct reads use
the same managed mechanism temporarily.

## Session lifecycle

A `Session` is closed by default and does not touch terminal state until `open()`:

```cpp
GameWIP::Terminal::Session session;

GameWIP::Terminal::Types::SessionOptions options;
options.deliveryMode =
    GameWIP::Terminal::Types::Input::DeliveryMode::Stream;

const auto status = session.open(options);
```

`open()` validates the selected input/output streams and policies, claims exclusive managed ownership of stdin, observes input capabilities, captures
exact native terminal input state when required, and applies the state required by the selected session policy.

Opening an already-open object returns `AlreadyOpen`. A second session or a direct read competing for managed stdin ownership returns `ResourceBusy`.

`close()` first restores persistent output state explicitly owned by the Session in reverse activation order, then restores the exact captured native
input state before releasing ownership. Closing an already-closed session succeeds. If explicit restoration fails, `close()` returns that failure
while the session remains open and retains ownership so cleanup can be retried. Successfully completed restoration steps are not repeated on retry.
Destruction instead attempts each pending output restoration once in reverse order, then attempts input restoration and releases bookkeeping;
destructor-time failures cannot be reported or retried after the object is gone.

The destructor is `noexcept` and makes best-effort output and input restoration attempts. Because no caller remains to retry afterward, destruction
releases process-wide input ownership even if restoration fails.

## Delivery mode

`SessionOptions::deliveryMode` is fixed for the complete open session:

- `Types::Input::DeliveryMode::Events` accepts `readEvent()`;
- `Types::Input::DeliveryMode::Stream` accepts `readBytes()`, `readText()`, and `readLine()`.

An incompatible read returns `Unsupported` without consuming input.

The explicit-session default is `Events`. Real Win32 consoles implement this mode directly from `INPUT_RECORD`; Stream sessions use the same immediate
native engine and rebuild byte/text/line semantics above the logical decoder instead of falling back to cooked console reads.

## Control-key policy

`SessionOptions::controlKeyMode` selects:

- `NativeProcessing`: preserve ordinary platform processing such as normal Ctrl+C behavior;
- `ReportAsInput`: request reportable control-key combinations as input.

This is a high-level policy, not exposure of native console flags.

## Native state is internal

Terminal disables native line buffering/echo for managed interactive console input and can change processed-input, resize-event, mouse-event, Quick
Edit, or backend protocol flags while it owns the endpoint. Those flags are implementation details behind the backend contract. The current Win32
native path enables desktop/resize records, disables mouse record generation and Quick Edit, and keeps VT-input translation disabled so `INPUT_RECORD`
retains release/repeat/location information. The public API does not expose:

- raw/cooked mode structures;
- echo toggles;
- native console flags;
- default-mode restoration functions;
- an availability-check-then-read preflight.

Direct reads capture and restore exact native state for one operation. Persistent sessions capture once and retain the state until close. Mouse
support is intentionally absent from the public event contract today, but the Win32 record dispatcher treats mouse as a distinct ignored record class
so a future portable mouse event can be added without redesigning the reader.

## Bound output and persistent output state

A Session binds `SessionOptions::output` for its complete open lifetime. Bound output writes, formatting, capability/geometry queries, styling, cursor
operations, clearing, scrolling, title, bell, and flush delegate to the same process-wide output implementation used by direct calls.

Opening a Session does not automatically hide the cursor or enter alternate screen. `Session::setCursorVisible(false)` and
`Session::enterAlternateScreen()` use the existing nesting-aware output-state machinery and record only the obligations created by that Session. Their
inverse operations remove the corresponding obligation when successful. `close()` replays any remaining obligations in actual reverse activation
order.

Input-consuming calls on one Session remain serialized with each other, but bound output may proceed from another thread while a read blocks. Each
call holds an active-operation lease without retaining the lifecycle mutex across backend or formatter work. `close()` stops unrelated operation
admission and waits for active Session operations before restoration; same-Session reentrant close returns `ResourceBusy` instead of waiting on
itself.

## Move behavior

`Session` is move-constructible and non-copyable. Move construction transfers the same private state object, so an open session keeps the same
process-wide ownership identity after the move.

Move assignment is deliberately deleted. Replacing an existing open destination could hide a restoration failure, so callers must close or destroy one
session explicitly instead.

The moved-from object is closed.

## Reads, deadlines, and cancellation

Every managed read supports the common public model described in @ref terminal_read_write.

- `std::nullopt` timeout waits indefinitely;
- `0ms` polls;
- positive timeout establishes one total deadline;
- negative timeout is `InvalidArgument`;
- `std::stop_token` requests cancellation.

A pre-requested stop returns `Types::Input::ReadOutcome::Cancelled` without consuming input. Blocking cancellation is accepted only when the endpoint
advertises `supportsCancellation`.

## Buffered input and handle replacement

Native mode transitions do not intentionally discard Terminal-owned pending UTF-8 bytes, unread native record batches, pending repeat events, key-down
state, or incomplete UTF-16 surrogate state. If the process replaces stdin, the Win32 backend recognizes the new endpoint identity and discards
pending state belonging to the old handle before reading the replacement.

Terminal cannot preserve unread input removed by an external native API, and external native mode changes bypass the managed ownership contract.

See @ref terminal_read_write and @ref terminal_capabilities_and_redirection.
