/// @file console.cpp
/// @brief Logger console style resolution and terminal output.

#include "logger/internal/core.h"
#include "logger/detail/formatting.h"

namespace GameWIP::Logger::Internal::Core
{
    /// ------------------------------------------------------------
    /// Console color resolution and rendering
    /// ------------------------------------------------------------

    static Terminal::Types::Style::Request terminalStyle(Color color)
    {
        Terminal::Types::Style::Request style;
        switch(color.kind)
        {
        case Color::Kind::Default:
            style.foreground = Terminal::defaultColor();
            break;
        case Color::Kind::Basic:
            style.foreground = Terminal::basicColor(static_cast<Terminal::Types::Style::BasicColor>(color.basic));
            break;
        case Color::Kind::Rgb:
            style.foreground = Terminal::rgbColor(color.red, color.green, color.blue);
            break;
        }
        return style;
    }

    Color levelColor(LogLevel level)
    {
        const Theme &theme = loggerState().theme;
        switch(level)
        {
        case LogLevel::Trace:
            return theme.trace;
        case LogLevel::Debug:
            return theme.debug;
        case LogLevel::Info:
            return theme.info;
        case LogLevel::Warn:
            return theme.warn;
        case LogLevel::Error:
            return theme.error;
        case LogLevel::Fatal:
            return theme.fatal;
        }
        return theme.message;
    }

    static Color markedValueColor(char role, Color explicitColor)
    {
        switch(role)
        {
        case 'C':
            return loggerState().theme.code;
        case 'P':
            return loggerState().theme.path;
        case 'N':
            return loggerState().theme.number;
        case 'Q':
            return loggerState().theme.quoted;
        case 'F':
            return loggerState().theme.field;
        case 'X':
            return explicitColor;
        default:
            return loggerState().theme.message;
        }
    }

    static Color semanticColor(char role, Color severityColor, Color sourceColor)
    {
        switch(role)
        {
        case 'T':
            return loggerState().theme.timestamp;
        case 'L':
            return severityColor;
        case 'A':
            return sourceColor;
        case 'M':
            return loggerState().theme.messageUsesSeverityColor ? severityColor : loggerState().theme.message;
        default:
            return markedValueColor(role, {});
        }
    }

    std::string plainOutputText(std::string_view text)
    {
        // Remove the record envelope, leaving only its message payload.
        if(!text.empty() && text.front() == Protocol::kLayoutBegin)
        {
            const std::size_t payloadBegin = text.find(Protocol::kLayoutBegin, 1);
            if(payloadBegin == std::string_view::npos)
            {
                return {};
            }
            text.remove_prefix(payloadBegin + 1);
        }

        std::string result;
        result.reserve(text.size());

        // Strip styling markers while copying visible text.
        for(std::size_t index = 0; index < text.size(); ++index)
        {
            if(text[index] == Protocol::kStyleBegin)
            {
                const std::size_t headerLength = index + 1 < text.size() && text[index + 1] == 'X' ? 7 : 2;
                index = std::min(text.size() - 1, index + headerLength - 1);
                continue;
            }

            if(text[index] == Protocol::kStyleScopeBegin)
            {
                ++index;
                continue;
            }

            if(text[index] != Protocol::kStyleEnd && text[index] != Protocol::kStyleScopeEnd && text[index] != Protocol::kLayoutBegin &&
               text[index] != Protocol::kLayoutSeparator)
            {
                result.push_back(text[index]);
            }
        }
        return result;
    }

    Status writeConsoleLine(const LogStyle &logStyle, std::string_view line, Color severityColor, Color sourceColor)
    {
        using Terminal::Types::Output::Segment;
        // Keep common layouts on the stack; grow only for unusually segmented templates.
        std::array<Segment, 16> fixedSegments{
            Terminal::textSegment({}),
            Terminal::textSegment({}),
            Terminal::textSegment({}),
            Terminal::textSegment({}),
            Terminal::textSegment({}),
            Terminal::textSegment({}),
            Terminal::textSegment({}),
            Terminal::textSegment({}),
            Terminal::textSegment({}),
            Terminal::textSegment({}),
            Terminal::textSegment({}),
            Terminal::textSegment({}),
            Terminal::textSegment({}),
            Terminal::textSegment({}),
            Terminal::textSegment({}),
            Terminal::textSegment({})};
        std::size_t fixedCount = 0;
        bool usesDynamicSegments = false;
        std::vector<Segment> dynamicSegments;
        const auto appendSegment = [&](Segment segment)
        {
            if(usesDynamicSegments)
            {
                dynamicSegments.push_back(segment);
            }
            else if(fixedCount < fixedSegments.size())
            {
                fixedSegments[fixedCount++] = segment;
            }
            else
            {
                dynamicSegments.reserve(fixedSegments.size() * 2);
                dynamicSegments.insert(dynamicSegments.end(), fixedSegments.begin(), fixedSegments.end());
                dynamicSegments.push_back(segment);
                usesDynamicSegments = true;
            }
        };

        // Resolve the destination and base message color once for this line.
        const auto stream = logStyle.useStderr ? Terminal::Types::Output::Stream::Stderr : Terminal::Types::Output::Stream::Stdout;
        const Color plainMessageColor = loggerState().theme.messageUsesSeverityColor ? severityColor : loggerState().theme.message;
        const auto styleFor = [](Color color)
        {
            return terminalStyle(color);
        };

        // Convert Logger style markers into terminal output segments.
        const auto appendStyledText = [&](std::string_view text, Color baseColor)
        {
            std::array<Color, Internal::kMaxLayoutColorScopeDepth> colorScopes{};
            std::size_t colorScopeDepth = 0;
            std::size_t plainStart = 0;
            std::size_t index = 0;
            const auto currentColor = [&]
            {
                return colorScopeDepth == 0 ? baseColor : colorScopes[colorScopeDepth - 1];
            };
            while(index < text.size())
            {
                // Push and pop nested semantic color scopes.
                if(text[index] == Protocol::kStyleScopeBegin && index + 1 < text.size())
                {
                    if(index > plainStart)
                    {
                        appendSegment(Terminal::styledTextSegment(text.substr(plainStart, index - plainStart), styleFor(currentColor())));
                    }

                    if(colorScopeDepth < colorScopes.size())
                    {
                        colorScopes[colorScopeDepth++] = semanticColor(text[index + 1], severityColor, sourceColor);
                    }
                    index += 2;
                    plainStart = index;
                    continue;
                }

                if(text[index] == Protocol::kStyleScopeEnd)
                {
                    if(index > plainStart)
                    {
                        appendSegment(Terminal::styledTextSegment(text.substr(plainStart, index - plainStart), styleFor(currentColor())));
                    }

                    if(colorScopeDepth > 0)
                    {
                        --colorScopeDepth;
                    }
                    ++index;
                    plainStart = index;
                    continue;
                }

                if(text[index] != Protocol::kStyleBegin)
                {
                    ++index;
                    continue;
                }

                if(index > plainStart)
                {
                    appendSegment(Terminal::styledTextSegment(text.substr(plainStart, index - plainStart), styleFor(currentColor())));
                }

                // Decode a marked value, including its optional explicit color.
                if(index + 1 >= text.size())
                {
                    index = text.size();
                    plainStart = index;
                    break;
                }
                const char role = text[index + 1];
                Color explicitColor{};
                std::size_t valueStart = index + 2;
                if(role == 'X')
                {
                    if(index + 7 > text.size())
                    {
                        index = text.size();
                        plainStart = index;
                        break;
                    }
                    explicitColor.kind = static_cast<Color::Kind>(static_cast<unsigned char>(text[index + 2]));
                    explicitColor.basic = static_cast<Color::Basic>(static_cast<unsigned char>(text[index + 3]));
                    explicitColor.red = static_cast<std::uint8_t>(text[index + 4]);
                    explicitColor.green = static_cast<std::uint8_t>(text[index + 5]);
                    explicitColor.blue = static_cast<std::uint8_t>(text[index + 6]);
                    valueStart = index + 7;
                }
                const std::size_t markerEnd = text.find(Protocol::kStyleEnd, valueStart);
                const std::size_t valueEnd = markerEnd == std::string_view::npos ? text.size() : markerEnd;
                const Color markedColor = role == 'X' ? explicitColor : semanticColor(role, severityColor, sourceColor);
                appendSegment(Terminal::styledTextSegment(text.substr(valueStart, valueEnd - valueStart), styleFor(markedColor)));
                index = markerEnd == std::string_view::npos ? text.size() : markerEnd + 1;
                plainStart = index;
            }

            if(plainStart < text.size())
            {
                appendSegment(Terminal::styledTextSegment(text.substr(plainStart), styleFor(currentColor())));
            }
        };

        appendStyledText(line, plainMessageColor);

        // Apply output policy and write the completed segments.
        Terminal::Types::Output::SegmentOptions options;
        options.styleMode = loggerState().consoleColorEnabledAtomic.load(std::memory_order_acquire) ? Terminal::Types::Style::Mode::Auto
                                                                                                    : Terminal::Types::Style::Mode::Never;
        options.appendLineEnding = true;
        options.lineEnding = Terminal::Types::Output::LineEnding::Native;
        options.flushMode =
            loggerState().flushConsoleEveryWriteAtomic.load(std::memory_order_acquire) ? IO::Types::FlushMode::Data : IO::Types::FlushMode::None;

        const std::span<const Segment> segments =
            usesDynamicSegments ? std::span<const Segment>(dynamicSegments) : std::span<const Segment>(fixedSegments).first(fixedCount);
        return Terminal::writeSegments(stream, segments, options).status();
    }

} // namespace GameWIP::Logger::Internal::Core
