/// @file report_templates.h
/// @brief Private templates backing Logger's synchronous report API.

#pragma once

#include "logger/detail/formatting.h"
#include "logger/record.h"

#include <chrono>
#include <format>
#include <new>
#include <string>
#include <string_view>
#include <utility>

namespace GameWIP::Logger
{
    /// @cond INTERNAL
    namespace Detail::Core
    {
        template <typename Source, typename... Args>
        Types::Report::Result formatAndReportWithRecord(
            Types::Level level,
            Source source,
            const Record &record,
            bool showPopup,
            const std::chrono::milliseconds *timeout,
            std::format_string<Args...> format,
            Args &&...args) noexcept
        {
            try
            {
                FormatScratchLease lease;
                std::string &scratch = lease.text();
                bool truncated = formatWithPolicy(scratch, getMaxMessageLengthForFormatting(), format, std::forward<Args>(args)...);
                if(truncated)
                {
                    removeTruncationSuffix(scratch);
                }

                scratch = RecordAccess::decorate(record, scratch, getFieldLayoutForFormatting(), getLayoutTemplateForFormatting());
                truncated = applyRecordMessageLimit(scratch, truncated, getMaxMessageLengthForFormatting());

                return reportPreformattedMessage(level, normalizeSource(source), scratch, showPopup, truncated, timeout);
            }
            catch(const std::format_error &)
            {
                recordFormatFailure();
                return reportFailure(IO::Types::ErrorCode::InvalidArgument);
            }
            catch(const std::bad_alloc &)
            {
                recordAllocationFailure();
                return reportFailure(IO::Types::ErrorCode::OutOfMemory);
            }
            catch(...)
            {
                recordAllocationFailure();
                return reportFailure(IO::Types::ErrorCode::Unknown);
            }
        }

        template <typename Source, typename... Args>
        Types::Report::Result runtimeFormatAndReportWithRecord(
            Types::Level level,
            Source source,
            const Record &record,
            bool showPopup,
            const std::chrono::milliseconds *timeout,
            Types::RuntimeFormat format,
            Args &...args) noexcept
        {
            try
            {
                FormatScratchLease lease;
                std::string &scratch = lease.text();
                bool truncated = runtimeFormatWithPolicy(scratch, getMaxMessageLengthForFormatting(), format, args...);
                if(truncated)
                {
                    removeTruncationSuffix(scratch);
                }

                scratch = RecordAccess::decorate(record, scratch, getFieldLayoutForFormatting(), getLayoutTemplateForFormatting());
                truncated = applyRecordMessageLimit(scratch, truncated, getMaxMessageLengthForFormatting());

                return reportPreformattedMessage(level, normalizeSource(source), scratch, showPopup, truncated, timeout);
            }
            catch(const std::format_error &)
            {
                recordFormatFailure();
                return reportFailure(IO::Types::ErrorCode::InvalidArgument);
            }
            catch(const std::bad_alloc &)
            {
                recordAllocationFailure();
                return reportFailure(IO::Types::ErrorCode::OutOfMemory);
            }
            catch(...)
            {
                recordAllocationFailure();
                return reportFailure(IO::Types::ErrorCode::Unknown);
            }
        }

        template <typename Source>
        Types::Report::Result reportPreformattedWithRecord(
            Types::Level level,
            Source source,
            const Record &record,
            bool showPopup,
            const std::chrono::milliseconds *timeout,
            std::string_view message) noexcept
        {
            try
            {
                if(GameWIP::Unicode::Utf8::validate(message).outcome != GameWIP::Unicode::Types::ValidationOutcome::Valid)
                {
                    return reportFailure(IO::Types::ErrorCode::EncodingFailed);
                }

                const std::size_t maxMessageLength = getMaxMessageLengthForFormatting();
                const bool inputTruncated = message.size() > maxMessageLength;
                const std::size_t boundedLength = utf8PrefixBoundary(message, maxMessageLength);
                std::string decorated =
                    RecordAccess::decorate(record, message.substr(0, boundedLength), getFieldLayoutForFormatting(), getLayoutTemplateForFormatting());
                const bool truncated = applyRecordMessageLimit(decorated, inputTruncated, maxMessageLength);

                return reportPreformattedMessage(level, normalizeSource(source), decorated, showPopup, truncated, timeout);
            }
            catch(const std::bad_alloc &)
            {
                recordAllocationFailure();
                return reportFailure(IO::Types::ErrorCode::OutOfMemory);
            }
            catch(...)
            {
                recordAllocationFailure();
                return reportFailure(IO::Types::ErrorCode::Unknown);
            }
        }
    } // namespace Detail::Core
    /// @endcond
} // namespace GameWIP::Logger
