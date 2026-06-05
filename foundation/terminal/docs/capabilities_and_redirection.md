@page terminal_capabilities_and_redirection Terminal capabilities and redirection

This page documents capability and redirection behavior.

The current implementation reports input and output capabilities for stdin/stdout/stderr and handles Windows real-console, redirected, detached, and other stream kinds.

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

The result type is `Types::InputCapabilityResult`.

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

The result type is `Types::OutputCapabilityResult`.

## StyleCapabilities

`Types::StyleCapabilities` reports portable style features rather than backend protocol details. It intentionally does not expose an ANSI flag.

## Redirection

Redirected input and output are byte-oriented.

Text helpers interpret redirected input bytes as UTF-8 and return `IO::Types::ErrorCode::EncodingFailed` when text conversion fails. Byte reads remain available for arbitrary input.

Redirected stdout and stderr should not receive styling bytes in `StyleMode::Auto`. `StyleMode::Always` may force styling only when the backend can honestly support the request; otherwise it reports `Unsupported`.

## Caching

Capability checks may be cached where safe. Cached capability data must not silently turn a known unsupported operation into a reported success.

Detached or unsupported streams should report capabilities accurately and return explicit status failures for operations that cannot run.
