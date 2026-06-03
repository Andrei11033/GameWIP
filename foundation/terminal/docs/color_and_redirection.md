@page foundation_terminal_color_and_redirection Terminal Color and Redirection

This page documents planned color and redirection behavior for `GameWIP::Terminal`.

No Terminal behavior is implemented in this pass.

## Capability detection

`getCapabilities()` reports whether a stream is attached, terminal-like, redirected, color-capable, ANSI-capable, and UTF-8-capable.

The capability result is observable through:

```cpp
struct Capabilities {
    bool isAttached = false;
    bool isTerminal = false;
    bool isRedirected = false;
    bool supportsUtf8Text = false;
    bool supportsColor = false;
    bool supportsAnsi = false;
};
```

## Color mode

`ColorMode::Never` writes plain text.

`ColorMode::Auto` writes styled text only when the stream supports styling. It writes plain text when styling is unsupported or redirected.

`ColorMode::Always` attempts to force styling. If styling cannot be forced, it reports `IO::Types::ErrorCode::Unsupported`.

## Styled writes

Styled writes should reset the stream style before returning.

If a styled write fails partway through, the implementation should still attempt a best-effort style reset before reporting the failure.

## Redirection

Redirected stdout and stderr are byte streams. Terminal should avoid injecting styling bytes into redirected output unless the caller explicitly requests styling through `ColorMode::Always` and the backend supports it.

## Stream serialization

Terminal API calls should serialize per stream so one `writeLine()` or `writeStyledLine()` call is not interleaved with another Terminal call to the same stream.

This does not protect against external writes made through `std::cout`, `printf`, or direct operating-system calls.
