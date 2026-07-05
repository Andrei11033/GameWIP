@page terminal_unicode_io Terminal Unicode I/O

Terminal presents one UTF-8 text contract while preserving byte-oriented access for redirected streams and callers that need arbitrary bytes.

## Public text

Public Terminal text is UTF-8 `std::string` and `std::string_view`.

Text reads preserve valid UTF-8 code point boundaries. Byte reads remain available when callers need arbitrary bytes.

Invalid UTF-8 observed by text helpers returns `IO::Types::ErrorCode::EncodingFailed`.

## Windows real console output

On Windows, real-console output accepts public UTF-8 text and displays the corresponding Unicode text. Terminal applies this behavior only when stdout or stderr is attached to a real console.

## Windows redirected output

On Windows, redirected stdout and stderr remain byte-oriented. Text writes preserve their UTF-8 bytes, and byte writes preserve arbitrary input bytes.

## Windows real console input

On Windows, real-console text input returns UTF-8.

## Windows redirected input

Redirected stdin is byte-oriented. Byte reads return bytes. Text and line helpers interpret those bytes as UTF-8.

## Failure behavior

Text operations reject invalid Unicode conversion with `EncodingFailed`. Byte operations do not perform text conversion or UTF-8 validation.
