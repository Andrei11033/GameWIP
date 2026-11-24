/// @file layout.cpp
/// @brief Record-envelope parsing and compiled layout rendering.

#include "logger/internal/core.h"
#include "logger/detail/formatting.h"

#include <algorithm>
#include <array>
#include <limits>
#include <memory>
#include <string>
#include <string_view>

namespace GameWIP::Logger::Internal::Core
{
    namespace
    {
        constexpr LayoutTemplate kDefaultLogLayout =
            makeLayout("{@timestamp}[{time}]{/}{@area}[{area:<auto}]{/}{@severity}[{level:<5}]{/} {message}");

        const std::string &compiledDefaultLogLayout()
        {
            static const std::string layout = Internal::compileLayoutTemplate(kDefaultLogLayout.text);
            return layout;
        }

        struct LayoutRecordData
        {
            std::string_view layout;
            std::string_view fields;
            std::string_view sourceFile;
            std::string_view sourceFunction;
            std::string_view sourceLine;
            std::string_view threadId;
            std::string_view processId;
        };

        bool readRecordEnvelope(std::string_view &message, LayoutRecordData &data)
        {
            if(message.empty() || message.front() != Protocol::kLayoutBegin)
            {
                return false;
            }
            const std::size_t layoutEnd = message.find(Protocol::kLayoutSeparator, 1);
            if(layoutEnd == std::string_view::npos)
            {
                return false;
            }
            std::size_t cursor = layoutEnd + 1;
            const std::size_t fieldsEnd = message.find(Protocol::kLayoutSeparator, cursor);
            if(fieldsEnd == std::string_view::npos)
            {
                return false;
            }

            // Read the layout and structured fields first.
            data.layout = message.substr(1, layoutEnd - 1);
            data.fields = message.substr(cursor, fieldsEnd - cursor);
            cursor = fieldsEnd + 1;

            // Read the fixed metadata fields and isolate the message payload.
            std::array<std::string_view *, 4> metadata{&data.sourceFile, &data.sourceFunction, &data.sourceLine, &data.threadId};
            for(std::string_view *value : metadata)
            {
                const std::size_t end = message.find(Protocol::kLayoutSeparator, cursor);
                if(end == std::string_view::npos)
                {
                    data = {};
                    return false;
                }
                *value = message.substr(cursor, end - cursor);
                cursor = end + 1;
            }

            const std::size_t processEnd = message.find(Protocol::kLayoutBegin, cursor);
            if(processEnd == std::string_view::npos)
            {
                data = {};
                return false;
            }
            data.processId = message.substr(cursor, processEnd - cursor);
            message.remove_prefix(processEnd + 1);
            return true;
        }

        void recoverMalformedRecordEnvelope(std::string_view &message, LayoutRecordData &data)
        {
            if(message.empty() || message.front() != Protocol::kLayoutBegin)
            {
                return;
            }
            const std::size_t payloadBegin = message.rfind(Protocol::kLayoutBegin);
            message =
                payloadBegin == 0 || payloadBegin == std::string_view::npos ? std::string_view("[truncated]") : message.substr(payloadBegin + 1);
            data = {};
        }

        std::string_view resolveLayoutValue(
            char placeholder,
            std::string_view timestamp,
            std::string_view levelText,
            std::string_view source,
            std::string_view message,
            const LayoutRecordData &data,
            char &role)
        {
            role = 0;
            switch(placeholder)
            {
            case 't':
                role = 'T';
                return timestamp;
            case 'l':
                role = 'L';
                return levelText;
            case 'a':
                role = 'A';
                return source;
            case 'm':
                return message;
            case 'F':
                return data.sourceFile;
            case 'u':
                return data.sourceFunction;
            case 'n':
                return data.sourceLine;
            case 'i':
                return data.threadId;
            case 'p':
                return data.processId;
            default:
                return {};
            }
        }

        void appendFormattedLayoutValue(
            std::string &outMessage,
            std::string_view value,
            char role,
            char name,
            char alignment,
            std::uint16_t requestedWidth,
            const std::array<char, Internal::kMaxLayoutColorScopeDepth> &activeColorRoles,
            std::size_t colorScopeDepth)
        {
            // Compute the requested field width and distribute its padding.
            std::size_t targetWidth = value.size();
            if(alignment != 0)
            {
                targetWidth = requestedWidth == std::numeric_limits<std::uint16_t>::max()
                                  ? (name == 'a' ? std::max(value.size(), loggerState().sourceNameWidth) : 5)
                                  : requestedWidth;
                targetWidth = std::max(targetWidth, value.size());
            }

            const std::size_t padding = targetWidth - value.size();
            const std::size_t leftPadding = alignment == '>' ? padding : alignment == '^' ? padding / 2 : 0;
            const std::size_t rightPadding = padding - leftPadding;
            const auto appendText = [&]
            {
                outMessage.append(leftPadding, ' ');
                outMessage.append(value);
                outMessage.append(rightPadding, ' ');
            };

            // Keep explicit field styling when the surrounding scope uses another role.
            if(role != 0 && (colorScopeDepth == 0 || activeColorRoles[colorScopeDepth - 1] != role))
            {
                outMessage.push_back(Protocol::kStyleBegin);
                outMessage.push_back(role);
                appendText();
                outMessage.push_back(Protocol::kStyleEnd);
                return;
            }
            appendText();
        }

        void renderCompiledLayout(
            std::string &outMessage,
            std::string_view layout,
            std::string_view timestamp,
            std::string_view levelText,
            std::string_view source,
            std::string_view message,
            const LayoutRecordData &data)
        {
            // Reset the output and track nested color scopes while interpreting instructions.
            std::array<char, Internal::kMaxLayoutColorScopeDepth> activeColorRoles{};
            std::size_t colorScopeDepth = 0;
            outMessage.clear();

            for(std::size_t index = 0; index < layout.size(); ++index)
            {
                // Copy literal text directly.
                if(layout[index] != Internal::kCompiledLayoutMarker)
                {
                    outMessage.push_back(layout[index]);
                    continue;
                }

                if(index + 1 >= layout.size())
                {
                    break;
                }
                const char operation = layout[++index];

                // Apply compiled color-scope instructions.
                if(operation == 'E')
                {
                    outMessage.push_back(Protocol::kStyleScopeEnd);
                    if(colorScopeDepth > 0)
                    {
                        --colorScopeDepth;
                    }
                    continue;
                }

                if(operation == 'S')
                {
                    if(index + 1 >= layout.size())
                    {
                        break;
                    }
                    const char role = layout[++index];
                    outMessage.push_back(Protocol::kStyleScopeBegin);
                    outMessage.push_back(role);
                    if(colorScopeDepth < activeColorRoles.size())
                    {
                        activeColorRoles[colorScopeDepth++] = role;
                    }
                    continue;
                }

                char placeholder = operation;
                char alignment = 0;
                std::uint16_t requestedWidth = 0;
                if(operation == '=')
                {
                    if(index + 4 >= layout.size())
                    {
                        break;
                    }
                    placeholder = layout[++index];
                    alignment = layout[++index];
                    const auto low = static_cast<unsigned char>(layout[++index]);
                    const auto high = static_cast<unsigned char>(layout[++index]);
                    requestedWidth = static_cast<std::uint16_t>(static_cast<std::uint32_t>(low) | (static_cast<std::uint32_t>(high) << 8u));
                }

                // Substitute fields or append the resolved layout value.
                if(placeholder == 'f')
                {
                    outMessage.append(data.fields);
                    continue;
                }

                char role = 0;
                const std::string_view value = resolveLayoutValue(placeholder, timestamp, levelText, source, message, data, role);

                if(placeholder == 't' || placeholder == 'l' || placeholder == 'a' || placeholder == 'm' || placeholder == 'F' || placeholder == 'u' ||
                   placeholder == 'n' || placeholder == 'i' || placeholder == 'p')
                {
                    appendFormattedLayoutValue(outMessage, value, role, placeholder, alignment, requestedWidth, activeColorRoles, colorScopeDepth);
                }
            }
        }
    } // namespace

    void buildLogLine(
        std::string &outMessage,
        std::string_view timestamp,
        std::string_view levelText,
        std::string_view source,
        std::string_view message)
    {
        const std::shared_ptr<const std::string> configuredLayout = loggerState().layoutTemplate.load(std::memory_order_acquire);
        LayoutRecordData record;
        const bool hasRecordEnvelope = readRecordEnvelope(message, record);
        if(!hasRecordEnvelope)
        {
            recoverMalformedRecordEnvelope(message, record);
        }

        // Prefer the record layout, then the configured and built-in layouts.
        std::string_view selectedLayout = hasRecordEnvelope  ? record.layout
                                          : configuredLayout ? std::string_view(*configuredLayout)
                                                             : std::string_view{};
        if(selectedLayout.empty())
        {
            selectedLayout = compiledDefaultLogLayout();
        }

        renderCompiledLayout(outMessage, selectedLayout, timestamp, levelText, source, message, record);
    }
} // namespace GameWIP::Logger::Internal::Core
