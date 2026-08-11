@page terminal_input_modes Sessions and managed input ownership

Terminal no longer exposes public native input-mode mutation. `Session` is the public persistent ownership/configuration boundary; direct reads use the same managed mechanism temporarily.

## Session lifecycle

A `Session` is closed by default and does not touch terminal state until `open()`:

```cpp
GameWIP::Terminal::Session session;

GameWIP::Terminal::Types::SessionOptions options;
options.deliveryMode =
    GameWIP::Terminal::Types::InputDeliveryMode::Stream;

const auto status = session.open(options);
```

`open()` validates the selected input/output streams and policies, claims exclusive managed ownership of stdin, observes input capabilities, captures exact native terminal input state when required, and applies the state required by the selected session policy.

Opening an already-open object returns `AlreadyOpen`. A second session or a direct read competing for managed stdin ownership returns `ResourceBusy`.

`close()` restores the exact captured native input state before releasing ownership. Closing an already-closed session succeeds. If explicit restoration fails, `close()` returns that failure while the session remains open and retains ownership so cleanup can be retried.

The destructor is `noexcept` and makes one best-effort restoration attempt. Because no caller remains to retry afterward, destruction releases process-wide ownership even if native restoration fails.

## Delivery mode

`SessionOptions::deliveryMode` is fixed for the complete open session:

- `InputDeliveryMode::Events` accepts `readEvent()`;
- `InputDeliveryMode::Stream` accepts `readBytes()`, `readText()`, and `readLine()`.

An incompatible read returns `Unsupported` without consuming input.

The explicit-session default is `Events`. On the current Win32 backend this mode remains `Unsupported` for real console input until the following #57 structured `INPUT_RECORD` backend slice lands. Capability reporting remains false until the backend exists.

## Control-key policy

`SessionOptions::controlKeyMode` selects:

- `NativeProcessing`: preserve ordinary platform processing such as normal Ctrl+C behavior;
- `ReportAsInput`: request reportable control-key combinations as input.

This is a high-level policy, not exposure of native console flags.

## Native state is internal

Terminal may disable native line buffering/echo or change processed-input behavior while it owns a real terminal. Those flags are implementation details behind the backend contract. The public API does not expose:

- raw/cooked mode structures;
- echo toggles;
- native console flags;
- default-mode restoration functions;
- an availability-check-then-read preflight.

Direct reads capture and restore exact native state for one operation. Persistent sessions capture once and retain the state until close.

## Move behavior

`Session` is move-constructible and non-copyable. Move construction transfers the same private state object, so an open session keeps the same process-wide ownership identity after the move.

Move assignment is deliberately deleted. Replacing an existing open destination could hide a restoration failure, so callers must close or destroy one session explicitly instead.

The moved-from object is closed.

## Reads, deadlines, and cancellation

Every managed read supports the common public model described in @ref terminal_read_write.

- `std::nullopt` timeout waits indefinitely;
- `0ms` polls;
- positive timeout establishes one total deadline;
- negative timeout is `InvalidArgument`;
- `std::stop_token` requests cancellation.

A pre-requested stop returns `ReadOutcome::Cancelled` without consuming input. Blocking cancellation is accepted only when the endpoint advertises `supportsCancellation`.

## Buffered input and handle replacement

Native mode transitions do not intentionally discard Terminal-owned pending UTF-8 bytes or incomplete conversion state. If the process replaces stdin, the Win32 backend recognizes the new endpoint identity and discards pending state belonging to the old handle before reading the replacement.

Terminal cannot preserve unread input removed by an external native API, and external native mode changes bypass the managed ownership contract.

See @ref terminal_read_write and @ref terminal_capabilities_and_redirection.
