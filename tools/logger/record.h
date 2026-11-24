/// @file record.h
/// @brief Public record metadata builder.

#pragma once

#include "logger/config.h"
#include "logger/detail/formatting.h"
#include "logger/logger_export.h"
#include "unicode/unicode.h"

#include <algorithm>
#include <cstddef>
#include <format>
#include <iterator>
#include <limits>
#include <source_location>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace GameWIP::Logger
{
    /// @cond INTERNAL
    namespace Internal::Core
    {
        struct RecordAccess;
    }
    /// @endcond

    /// @brief Optional per-record fields and runtime metadata for rich log messages.
    class Record
    {
    public:
        // Field values
        /// @brief Adds a formatted key-value field when the key and configured limits allow it.
        template <typename Value> Record &field(std::string_view key, Value &&value)
        {
            // Reject invalid keys and fields beyond the configured limit.
            if(key.empty() || GameWIP::Unicode::Utf8::validate(key).outcome != GameWIP::Unicode::Types::ValidationOutcome::Valid)
            {
                return *this;
            }

            const Internal::Core::RecordLimits limits = Internal::Core::getRecordLimitsForFormatting();
            if(fields_.size() >= limits.maxFields)
            {
                return *this;
            }

            // Bound the key and format the value within its configured limit.
            key = key.substr(0, Internal::Core::utf8PrefixBoundary(key, limits.maxKeyLength));
            if(key.empty())
            {
                return *this;
            }

            std::string text;
            // Include room for a four-byte UTF-8 scalar that crosses the retained boundary.
            const std::size_t outputLimit = limits.maxValueLength == 0                                            ? 0
                                            : limits.maxValueLength > std::numeric_limits<std::size_t>::max() - 3 ? limits.maxValueLength
                                                                                                                  : limits.maxValueLength + 3;
            const std::size_t maxIteratorDistance = static_cast<std::size_t>(std::numeric_limits<std::ptrdiff_t>::max());
            const std::ptrdiff_t boundedOutputLimit = static_cast<std::ptrdiff_t>(std::min(outputLimit, maxIteratorDistance));
            static_cast<void>(std::format_to_n(std::back_inserter(text), boundedOutputLimit, "{}", std::forward<Value>(value)));
            text.resize(Internal::Core::utf8PrefixBoundary(text, limits.maxValueLength));

            // Retain only valid UTF-8 field values.
            if(GameWIP::Unicode::Utf8::validate(text).outcome != GameWIP::Unicode::Types::ValidationOutcome::Valid)
            {
                return *this;
            }

            fields_.emplace_back(key, std::move(text));
            return *this;
        }

        // Source metadata

        /// @brief Includes the current thread identifier in this record.
        Record &threadId() noexcept
        {
            includeThreadId_ = true;
            return *this;
        }

        /// @brief Includes the current process identifier in this record.
        Record &processId() noexcept
        {
            includeProcessId_ = true;
            return *this;
        }

        /// @brief Includes the source file from the call site.
        Record &file() noexcept
        {
            includeFile_ = true;
            return *this;
        }

        /// @brief Includes the function name from the call site.
        Record &function() noexcept
        {
            includeFunction_ = true;
            return *this;
        }

        /// @brief Includes the source line from the call site.
        Record &line() noexcept
        {
            includeLine_ = true;
            return *this;
        }

        /// @brief Includes file, function, and line from the call site.
        Record &sourceLocation() noexcept
        {
            includeFile_ = true;
            includeFunction_ = true;
            includeLine_ = true;
            return *this;
        }

        // Field layout

        /// @brief Selects a built-in layout for this record's fields.
        Record &layout(FieldLayout value) noexcept
        {
            layout_ = Internal::isValidFieldLayout(value) ? value : FieldLayout::Adaptive;
            hasLayout_ = true;
            return *this;
        }

        /// @brief Selects a validated custom layout template for this record.
        Record &layout(LayoutTemplate value)
        {
            customLayout_ = Internal::isValidLayoutTemplate(value.text) &&
                                    GameWIP::Unicode::Utf8::validate(value.text).outcome == GameWIP::Unicode::Types::ValidationOutcome::Valid
                                ? Internal::compileLayoutTemplate(value.text)
                                : std::string{};
            return *this;
        }

    private:
        explicit Record(std::source_location location) noexcept
            : location_(location)
        {
        }

        // Message formatting

        [[nodiscard]] std::string prefixContent() const
        {
            // Start with the formatted record fields.
            std::string result = fieldText();

            // Append selected call-site and runtime metadata.
            if(includeFile_ || includeFunction_ || includeLine_ || includeThreadId_ || includeProcessId_)
            {
                // Separate existing fields from optional metadata.
                if(!result.empty())
                {
                    result.append(" | ");
                }

                // Format the selected call-site location.
                if(includeFile_)
                {
                    result.append(location_.file_name());
                }

                if(includeLine_)
                {
                    if(includeFile_)
                    {
                        result.push_back(':');
                    }
                    result.append(std::format("{}", location_.line()));
                }

                if(includeFunction_)
                {
                    if(includeFile_ || includeLine_)
                    {
                        result.push_back(' ');
                    }
                    result.append(location_.function_name());
                }

                // Append selected runtime identifiers.
                if(includeThreadId_)
                {
                    if(!result.empty())
                    {
                        result.push_back(' ');
                    }
                    result.append(std::format("thread={}", std::this_thread::get_id()));
                }

                if(includeProcessId_)
                {
                    if(!result.empty())
                    {
                        result.push_back(' ');
                    }
                    result.append(std::format("process={}", Internal::Core::currentProcessId()));
                }
            }

            return result;
        }

        [[nodiscard]] std::string fieldText() const
        {
            std::string result;
            for(const auto &[key, value] : fields_)
            {
                if(!result.empty())
                {
                    result.push_back(' ');
                }
                result.push_back(Internal::Core::Protocol::kStyleBegin);
                result.push_back('F');
                result.append(key);
                result.push_back(Internal::Core::Protocol::kStyleEnd);
                result.push_back('=');
                result.append(value);
            }
            return result;
        }

        [[nodiscard]] std::string decorate(std::string_view message, FieldLayout defaultLayout, LayoutTemplate defaultTemplate = {}) const
        {
            constexpr std::size_t kAdaptiveExpansionThreshold = 96;
            const std::string_view selectedTemplate = customLayout_.empty() ? defaultTemplate.text : std::string_view(customLayout_);

            // Apply a validated custom template when one is available.
            if(!selectedTemplate.empty())
            {
                const std::string fields = fieldText();
                std::string result;
                result.reserve(selectedTemplate.size() + fields.size() + message.size() + 8);

                // Store the selected layout and structured fields.
                result.push_back(Internal::Core::Protocol::kLayoutBegin);
                result.append(selectedTemplate);
                result.push_back(Internal::Core::Protocol::kLayoutSeparator);
                result.append(fields);
                result.push_back(Internal::Core::Protocol::kLayoutSeparator);

                // Serialize optional source and runtime metadata in envelope order.
                const auto appendMetadata = [&result](std::string_view value)
                {
                    result.append(value);
                    result.push_back(Internal::Core::Protocol::kLayoutSeparator);
                };
                appendMetadata(includeFile_ ? location_.file_name() : std::string_view{});
                appendMetadata(includeFunction_ ? location_.function_name() : std::string_view{});
                appendMetadata(includeLine_ ? std::format("{}", location_.line()) : std::string{});
                appendMetadata(includeThreadId_ ? std::format("{}", std::this_thread::get_id()) : std::string{});
                result.append(includeProcessId_ ? std::format("{}", Internal::Core::currentProcessId()) : std::string{});

                // Close the metadata envelope before appending the message.
                result.push_back(Internal::Core::Protocol::kLayoutBegin);
                result.append(message);
                return result;
            }

            // Build prefix content before selecting the configured field layout.
            std::string prefixText = prefixContent();

            // Use the built-in layout when no custom template was selected.
            if(prefixText.empty())
            {
                return std::string(message);
            }

            const FieldLayout selected = hasLayout_ ? layout_ : defaultLayout;
            bool expanded = selected == FieldLayout::Expanded;
            if(selected == FieldLayout::Adaptive)
            {
                std::size_t totalFieldBytes = 0;
                for(const auto &[key, value] : fields_)
                {
                    totalFieldBytes += key.size() + value.size() + 1;
                    expanded = expanded || value.contains('\n');
                }
                expanded = expanded || totalFieldBytes > kAdaptiveExpansionThreshold;
            }

            // Render either a multiline record or the selected inline layout.
            if(expanded)
            {
                std::string result(message);
                result.append("\n    ");
                result.append(prefixText);
                return result;
            }

            if(selected == FieldLayout::Adaptive || selected == FieldLayout::Parenthesized)
            {
                return std::format("{} ({})", message, prefixText);
            }
            return std::format("{}: {}", prefixText, message);
        }

        // Stored fields and call site
        std::vector<std::pair<std::string, std::string>> fields_;
        std::source_location location_;

        // Field layout selection
        FieldLayout layout_ = FieldLayout::Adaptive;
        std::string customLayout_;
        bool hasLayout_ = false;

        // Optional call-site metadata
        bool includeThreadId_ = false;
        bool includeProcessId_ = false;
        bool includeFile_ = false;
        bool includeFunction_ = false;
        bool includeLine_ = false;

        friend Record record(std::source_location location) noexcept;
        friend struct Internal::Core::RecordAccess;
    };

    /// @brief Starts building optional per-record metadata.
    [[nodiscard]] inline Record record(std::source_location location = std::source_location::current()) noexcept
    {
        return Record(location);
    }

    /// @cond INTERNAL
    namespace Internal::Core
    {
        struct RecordAccess
        {
            [[nodiscard]] static std::string decorate(
                const Record &record,
                std::string_view message,
                FieldLayout defaultLayout,
                LayoutTemplate defaultTemplate = {})
            {
                return record.decorate(message, defaultLayout, defaultTemplate);
            }

            [[nodiscard]] static std::string decorate(
                const Record &record,
                std::string_view message,
                FieldLayout defaultLayout,
                const std::shared_ptr<const std::string> &defaultTemplate)
            {
                const LayoutTemplate layout{defaultTemplate ? std::string_view(*defaultTemplate) : std::string_view{}};
                return record.decorate(message, defaultLayout, layout);
            }
        };
    } // namespace Internal::Core
    /// @endcond

    /// @cond INTERNAL
    namespace Internal::Core
    {
        template <typename Source>
        void preformattedLogWithRecord(Types::Level level, Source source, const Record &record, std::string_view message) noexcept
        {
            try
            {
                const std::size_t maxMessageLength = getMaxMessageLengthForFormatting();
                const bool inputTruncated = message.size() > maxMessageLength;
                const std::size_t boundedLength = utf8PrefixBoundary(message, maxMessageLength);
                const std::string_view boundedMessage = message.substr(0, boundedLength);

                std::string decorated =
                    RecordAccess::decorate(record, boundedMessage, getFieldLayoutForFormatting(), getLayoutTemplateForFormatting());
                const bool truncated = applyRecordMessageLimit(decorated, inputTruncated, maxMessageLength);

                enqueuePreformattedMessage(level, normalizeSource(source), decorated, truncated);
            }
            catch(...)
            {
                recordAllocationFailure();
            }
        }
    } // namespace Internal::Core
    /// @endcond

} // namespace GameWIP::Logger
