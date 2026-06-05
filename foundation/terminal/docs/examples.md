@page terminal_examples Terminal examples

## Styled status line

```cpp
GameWIP::Terminal::Types::TextStyle style;
style.foreground = GameWIP::Terminal::basicColor(GameWIP::Terminal::Types::BasicColor::Green);
style.bold = true;

GameWIP::Terminal::Types::LineWriteOptions options;
options.styleMode = GameWIP::Terminal::Types::StyleMode::Auto;
options.style = style;

GameWIP::Terminal::writeLine("ready", options);
```

## Segmented output

```cpp
std::array segments{
    GameWIP::Terminal::textSegment("[info] "),
    GameWIP::Terminal::textSegment("loaded"),
};

GameWIP::Terminal::Types::SegmentWriteOptions options;
options.appendLineEnding = true;
GameWIP::Terminal::writeSegments(segments, options);
```

More behavior is documented in @ref terminal_read_write and @ref terminal_segmented_writes.
