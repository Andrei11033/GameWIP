/// @file log_templates.h
/// @brief Private templates backing Logger's asynchronous logging API.

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
        void formatAndLogWithRecord(
            Types::Level level,
            Source source,
            const Record &record,
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

                enqueuePreformattedMessage(level, normalizeSource(source), scratch, truncated);
            }
            catch(const std::format_error &)
            {
                recordFormatFailure();
            }
            catch(...)
            {
                recordAllocationFailure();
            }
        }

        template <typename Source, typename... Args>
        void runtimeFormatAndLogWithRecord(
            Types::Level level,
            Source source,
            const Record &record,
            Types::RuntimeFormat format,
            Args &&...args) noexcept
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

                enqueuePreformattedMessage(level, normalizeSource(source), scratch, truncated);
            }
            catch(const std::format_error &)
            {
                recordFormatFailure();
            }
            catch(...)
            {
                recordAllocationFailure();
            }
        }
    } // namespace Detail::Core
    /// @endcond
} // namespace GameWIP::Logger
