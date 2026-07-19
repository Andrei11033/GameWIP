@page terminal_capabilities_and_redirection Capabilities, preparation, and redirection

Capabilities describe the current stdin, stdout, or stderr endpoint. They are snapshots, not reservations: redirection, handle replacement, mode changes, or external native calls can make a previous result stale. The status returned by the requested operation remains authoritative.

## Stream kinds

| `StreamKind` | Meaning |
| --- | --- |
| `Detached` | No usable backend handle is currently attached. |
| `Terminal` | A real interactive terminal/console endpoint. |
| `Redirected` | A pipe, regular file, IDE capture stream, or similar byte endpoint. |
| `Other` | A valid endpoint that cannot be classified safely as terminal or redirected. |

A detached capability query can succeed and report `Detached`; an operation that requires an open endpoint can then return `NotOpen`. Do not infer terminal behavior from `Other`.

## Input capabilities

`InputCapabilities` reports stream kind, UTF-8 text, byte and line input, raw mode, echo control, input-mode operations, availability queries, and finite-timeout support. `InputCapabilitiesResult` pairs that snapshot with an IO status.

## Output capabilities

`OutputCapabilities` reports stream kind, UTF-8 text, byte output, flush support, portable styles, terminal size, cursor operations, clear/scroll, alternate screen, title, and bell support. `OutputCapabilitiesResult` pairs the snapshot with an IO status.

`supportsFlush` means Terminal accepts a flush request for the endpoint. It does not promise storage durability for every endpoint. On Win32, only standard handles redirected to regular disk files receive an operating-system flush; console and pipe requests are successful no-ops.

## Observation versus preparation

`getOutputCapabilities()` is observational and never changes terminal mode.

`prepareOutput()` enables backend state required by styling and controls. It is idempotent for the current handle. On Win32 real consoles it enables virtual-terminal processing where supported. That mode change persists for the current console handle; Terminal does not return an RAII object that restores preparation.

Preparation is not required for plain text. Redirected streams need no setup and return their redirected capabilities. Detached streams return `NotOpen`. stdout and stderr are prepared and tracked independently.

Styled writes and controls prepare lazily when their requested feature is not active. Explicit preparation is useful when startup wants to diagnose unsupported terminal behavior before first use.

## Redirection rules

Redirected endpoints are byte-oriented:

- text output preserves supplied UTF-8 bytes;
- byte output preserves arbitrary bytes where supported;
- text and line input interpret redirected bytes as UTF-8;
- byte input performs no text interpretation.

`StyleMode::Auto` omits style sequences on redirected output that does not advertise the requested style. `StyleMode::Required` returns `Unsupported` without normal text emission when the style cannot be honored.

## Current Win32 behavior

Real-console output converts UTF-8 to UTF-16 and uses the native Unicode console path. Redirected output writes bytes. The reported style set is conservative; enabling virtual-terminal processing does not by itself promise RGB, dim, italic, or strikethrough support.

Named-pipe input supports finite read timeouts. Real-console input preserves native cooked input, echo, and line editing and currently supports only `kWaitForever`. Regular redirected files support availability queries from their current position but do not promise bounded timeouts.

## Failure behavior

`StyleMode::Auto` falls back to plain text when preparation or style support is unavailable. `StyleMode::Required` and terminal controls return the preparation or capability failure without normal emission. Plain text, byte output, size queries, cursor-position queries, and input operations do not implicitly prepare output.

See @ref terminal_styling, @ref terminal_read_write, and @ref terminal_control_primitives.
