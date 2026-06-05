@page foundation_terminal_unicode_terminal_io Terminal Unicode I/O

This page documents Unicode behavior for Terminal text input and output.

The Windows backend converts public UTF-8 text to UTF-16 for real console output, converts real console input to UTF-8, and leaves redirected streams byte-oriented.

## Public text

Public Terminal text is UTF-8 `std::string` and `std::string_view`.

Terminal text APIs do not parse JSON, config files, assets, markup, or higher-level formats.

Text reads preserve valid UTF-8 code point boundaries. Byte reads remain available when callers need arbitrary bytes.

Invalid UTF-8 observed by text helpers should return `IO::Types::ErrorCode::EncodingFailed`.

## Windows real console output

On Windows, real console output should convert UTF-8 public text to UTF-16 and use explicit Unicode Win32 APIs such as:

```cpp
WriteConsoleW
```

The backend should choose this path only when stdout or stderr is attached to a real console handle that accepts console text output.

## Windows redirected output

On Windows, redirected stdout and stderr output should remain byte-oriented and use:

```cpp
WriteFile
```

Redirected process output carries bytes. Terminal must not force redirected output through UTF-16 console APIs.

## Windows real console input

On Windows, real console input should read native console text and convert it to UTF-8 at the backend boundary.

## Windows redirected input

Redirected stdin is byte-oriented. Byte reads return bytes. Text and line helpers interpret those bytes as UTF-8.

## Explicit Windows API rule

Windows backend code must use explicit Unicode Win32 APIs where text APIs are needed.

Do not use ANSI APIs.

Do not use generic macro-mapped A/W APIs.

Core Terminal code must not call Win32 APIs directly.
