@page foundation_terminal Terminal

`GameWIP::Terminal` is the planned portable public API for stdout and stderr terminal output.

This page documents the intended public API only. No Terminal behavior or backend is implemented in this pass.

## Documentation sections

- @subpage foundation_terminal_color_and_redirection
- @subpage foundation_terminal_unicode_terminal_output

## Purpose

`GameWIP::Terminal` owns stdout and stderr output behavior. It depends on `GameWIP::IO` for shared status and write-result concepts.

Terminal is output-only in v1.

## Namespace

```cpp
namespace GameWIP::Terminal;
namespace GameWIP::Terminal::Types;
```

Passive public data shapes live under `GameWIP::Terminal::Types`. Active APIs live directly under `GameWIP::Terminal`.

## Planned passive types

```cpp
namespace GameWIP::Terminal::Types {

enum class Stream {
    Stdout,
    Stderr
};

enum class ColorMode {
    Never,
    Auto,
    Always
};

enum class Color {
    Default,

    Black,
    Red,
    Green,
    Yellow,
    Blue,
    Magenta,
    Cyan,
    White,

    BrightBlack,
    BrightRed,
    BrightGreen,
    BrightYellow,
    BrightBlue,
    BrightMagenta,
    BrightCyan,
    BrightWhite
};

struct TextStyle {
    Color foreground = Color::Default;
    Color background = Color::Default;
    bool bold = false;
    bool underline = false;
};

struct Capabilities {
    bool isAttached = false;
    bool isTerminal = false;
    bool isRedirected = false;
    bool supportsUtf8Text = false;
    bool supportsColor = false;
    bool supportsAnsi = false;
};

struct CapabilityResult {
    IO::Types::Status status;
    Capabilities capabilities;
};

struct WriteOptions {
    ColorMode colorMode = ColorMode::Auto;
    bool flushAfterWrite = false;
};

} // namespace GameWIP::Terminal::Types
```

## Planned active API

```cpp
namespace GameWIP::Terminal {

class StreamWriter;

[[nodiscard]] Types::CapabilityResult getCapabilities(
    Types::Stream stream);

[[nodiscard]] IO::Types::WriteResult writeBytes(
    Types::Stream stream,
    std::span<const std::byte> bytes);

[[nodiscard]] IO::Types::Status writeText(
    Types::Stream stream,
    std::string_view utf8Text,
    const Types::WriteOptions& options = {});

[[nodiscard]] IO::Types::Status writeLine(
    Types::Stream stream,
    std::string_view utf8Text = {},
    const Types::WriteOptions& options = {});

[[nodiscard]] IO::Types::Status writeStyled(
    Types::Stream stream,
    std::string_view utf8Text,
    const Types::TextStyle& style,
    const Types::WriteOptions& options = {});

[[nodiscard]] IO::Types::Status writeStyledLine(
    Types::Stream stream,
    std::string_view utf8Text,
    const Types::TextStyle& style,
    const Types::WriteOptions& options = {});

[[nodiscard]] IO::Types::Status flush(
    Types::Stream stream);

} // namespace GameWIP::Terminal
```

## Contract notes

`GameWIP::Terminal` owns stdout and stderr behavior only.

It does not own Windows popups, Assert dialogs, debugger output, terminal input, cursor movement, progress bars, or TUI behavior.

Terminal API calls should serialize per stream so one Terminal call does not interleave with another Terminal call.

No guarantee is made against interleaving with raw `std::cout`, `printf`, or direct operating-system writes.
