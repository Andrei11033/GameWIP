@page terminal_capabilities_and_redirection Terminal capabilities and redirection

Terminal reports capabilities for stdin, stdout, and stderr across real terminals, redirected streams, detached streams, and other platform endpoints.

## StreamKind

`Types::StreamKind` replaces separate attached, terminal, and redirected boolean combinations.

| Kind | Meaning |
| --- | --- |
| `Detached` | The stream is missing, detached, or has no valid backend handle. |
| `Terminal` | The stream is attached to a real terminal or console. |
| `Redirected` | The stream is redirected to or from a pipe, file, IDE capture stream, or similar endpoint. |
| `Other` | The stream exists but does not fit the normal terminal or redirected categories. |

## Input capabilities

`Types::InputCapabilities` reports:

- stream kind;
- UTF-8 text input support;
- byte input support;
- line input support;
- raw byte input mode support;
- echo control support;
- input mode query/change support;
- input availability query support;
- read timeout support.

The result type is `Types::InputCapabilitiesResult`.

## Output capabilities

`Types::OutputCapabilities` reports:

- stream kind;
- UTF-8 text output support;
- byte output support;
- flush support;
- style capabilities;
- terminal size query support;
- cursor movement and cursor position query support;
- cursor save/restore support;
- cursor visibility support;
- clear, scroll, alternate screen, title, and bell support.

The result type is `Types::OutputCapabilitiesResult`.

`supportsFlush` means the stream accepts Terminal flush requests. It does not imply a storage durability operation for every stream kind. On Win32, regular redirected files use `FlushFileBuffers`; console and pipe flushes are successful no-ops because Terminal writes directly to their native handles.

`getOutputCapabilities()` is observational. It inspects the current stream state and never enables terminal features.

`prepareOutput()` enables platform support required by styles and terminal controls. Preparation is idempotent. Redirected streams require no setup and return their normal capabilities; detached streams return `IO::Types::ErrorCode::NotOpen`. A capability query after successful preparation reports the support that is currently active.

Styled writes and terminal controls prepare lazily when their required capability is not active. Explicit preparation is useful during startup when the application wants to report configuration failures before its first styled or interactive output.

## StyleCapabilities

`Types::StyleCapabilities` reports portable style features rather than backend protocol details. It intentionally does not expose an ANSI flag.

## Redirection

Redirected input and output are byte-oriented.

Text helpers interpret redirected input bytes as UTF-8 and return `IO::Types::ErrorCode::EncodingFailed` when text conversion fails. Byte reads remain available for arbitrary input.

Redirected stdout and stderr should not receive styling bytes in `StyleMode::Auto`. `StyleMode::Required` may force styling only when the backend can honestly support the request; otherwise it reports `Unsupported`.

On Win32, real-console preparation enables virtual-terminal processing. Redirected UTF-8 output is written byte-for-byte and does not require console preparation.

Win32 style capabilities are conservative. The real-console backend reports only portable features documented for the console VT implementation. In particular, RGB, dim, italic, and strikethrough are not promised merely because VT processing is enabled.

Win32 named-pipe input supports finite read timeouts. Real-console input preserves native cooked input, echo, and line editing through `ReadConsoleW`, so it supports only `kWaitForever`; finite and non-blocking console reads return `Unsupported`. Regular redirected files support availability queries from their current position but do not promise bounded read timeouts.

## Failure behavior

`StyleMode::Auto` falls back to plain text when preparation fails. `StyleMode::Required` and terminal controls return the preparation or unsupported status without writing. Plain text, byte output, size queries, cursor-position queries, and input operations never prepare output.
