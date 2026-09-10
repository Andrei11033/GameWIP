/// @file formatting.h
/// @brief Installed implementation-detail support for Logger formatting templates.

#pragma once

#include "logger/config.h"
#include "logger/logger_export.h"

#include <chrono>
#include <cstddef>
#include <format>
#include <iterator>
#include <new>
#include <string>
#include <string_view>
#include <utility>

namespace GameWIP::Logger
{
    /// @cond INTERNAL
    namespace Detail::Core
    {
        /// @brief Converts enum sources to SourceId while preserving string-like sources.
        template <typename Source> constexpr decltype(auto) normalizeSource(Source &&source) noexcept
        {
            if constexpr (isSourceEnum<Source>)
            {
                return sourceId(source);
            }
            else
            {
                return std::forward<Source>(source);
            }
        }

        // Formatting templates end by crossing into exported non-template code.
        // This keeps queue/report ownership rules in one implementation unit.
        GAMEWIP_LOGGER_EXPORT void enqueuePreformattedMessage(Types::Level level, std::string_view source, std::string_view message) noexcept;
        GAMEWIP_LOGGER_EXPORT void enqueuePreformattedMessage(
            Types::Level level,
            std::string_view source,
            std::string_view message,
            bool alreadyTruncated) noexcept;
        GAMEWIP_LOGGER_EXPORT void enqueuePreformattedMessage(Types::Level level, Types::SourceId source, std::string_view message) noexcept;
        GAMEWIP_LOGGER_EXPORT void enqueuePreformattedMessage(
            Types::Level level,
            Types::SourceId source,
            std::string_view message,
            bool alreadyTruncated) noexcept;

        GAMEWIP_LOGGER_EXPORT Types::Report::Result reportPreformattedMessage(
            Types::Level level,
            std::string_view source,
            std::string_view message,
            bool showPopup,
            bool alreadyTruncated,
            const std::chrono::milliseconds *timeout) noexcept;
        GAMEWIP_LOGGER_EXPORT Types::Report::Result reportPreformattedMessage(
            Types::Level level,
            Types::SourceId source,
            std::string_view message,
            bool showPopup,
            bool alreadyTruncated,
            const std::chrono::milliseconds *timeout) noexcept;

        GAMEWIP_LOGGER_EXPORT void recordAllocationFailure() noexcept;
        GAMEWIP_LOGGER_EXPORT void recordFormatFailure() noexcept;
        GAMEWIP_LOGGER_EXPORT std::string &formatScratch();
        GAMEWIP_LOGGER_EXPORT std::size_t getMaxMessageLengthForFormatting();
        GAMEWIP_LOGGER_EXPORT Types::FormatPolicy getFormatPolicyForFormatting();
        GAMEWIP_LOGGER_EXPORT void releaseFormatScratchIfNeeded(std::string &scratch) noexcept;

        /// @brief RAII handle for one nested use of the per-thread formatting scratch buffer.
        /// @details Public formatting templates use this so early returns and exceptions release scratch consistently.
        class FormatScratchLease
        {
        public:
            FormatScratchLease()
                : scratch_(formatScratch())
            {
            }
            ~FormatScratchLease() noexcept
            {
                releaseFormatScratchIfNeeded(scratch_);
            }
            FormatScratchLease(const FormatScratchLease &) = delete;
            FormatScratchLease &operator=(const FormatScratchLease &) = delete;
            [[nodiscard]] std::string &text() noexcept
            {
                return scratch_;
            }

        private:
            std::string &scratch_;
        };

        // ------------------------------------------------------------
        // Message truncation
        // ------------------------------------------------------------

        /// @brief Finds a truncation boundary that does not split a UTF-8 continuation sequence.
        [[nodiscard]] inline std::size_t utf8PrefixBoundary(std::string_view text, std::size_t limit) noexcept
        {
            if (limit >= text.size())
            {
                return text.size();
            }
            std::size_t boundary = limit;
            while (boundary > 0)
            {
                const auto value = static_cast<unsigned char>(text[boundary]);
                if ((value & 0xC0u) != 0x80u)
                {
                    break;
                }
                --boundary;
            }
            return boundary;
        }

        /// @brief Appends the visible truncation marker while respecting very small message limits.
        inline void appendTruncationSuffix(std::string &scratch, std::size_t maxMessageLength)
        {
            constexpr std::string_view suffix = "... [truncated]";
            if (maxMessageLength == 0)
            {
                scratch.clear();
                return;
            }
            if (maxMessageLength <= suffix.size())
            {
                scratch.assign(suffix.substr(0, maxMessageLength));
                return;
            }
            scratch.append(suffix);
        }

        /// @brief Rewrites scratch to the retained prefix plus truncation marker.
        inline void truncateScratch(std::string &scratch, std::size_t maxMessageLength)
        {
            constexpr std::string_view suffix = "... [truncated]";
            if (maxMessageLength > suffix.size())
            {
                const std::size_t budget = maxMessageLength - suffix.size();
                scratch.resize(utf8PrefixBoundary(scratch, budget));
            }
            else
            {
                scratch.clear();
            }
            appendTruncationSuffix(scratch, maxMessageLength);
        }

        /// @brief Applies Logger's retained-message byte limit after an unbounded formatting pass.
        inline bool truncateScratchIfNeeded(std::string &scratch, std::size_t maxMessageLength)
        {
            if (scratch.size() <= maxMessageLength)
            {
                return false;
            }
            truncateScratch(scratch, maxMessageLength);
            return true;
        }

        // ------------------------------------------------------------
        // Bounded formatting
        // ------------------------------------------------------------

        /// @brief Output iterator that counts every formatted byte and stores only the configured prefix.
        /// @details StrictBounded formatting avoids building a full oversized message before truncation.
        class BoundedFormatIterator
        {
        public:
            using difference_type = std::ptrdiff_t;
            using value_type = char;
            using pointer = void;
            using reference = void;
            using iterator_category = std::output_iterator_tag;

            BoundedFormatIterator(std::string &output, std::size_t limit, std::size_t &written, bool &truncated) noexcept
                : output_(&output)
                , limit_(limit)
                , written_(&written)
                , truncated_(&truncated)
            {
            }
            BoundedFormatIterator &operator=(char value)
            {
                if (*written_ < limit_)
                {
                    output_->push_back(value);
                }
                else
                {
                    *truncated_ = true;
                }
                ++*written_;
                return *this;
            }
            BoundedFormatIterator &operator*() noexcept
            {
                return *this;
            }
            BoundedFormatIterator &operator++() noexcept
            {
                return *this;
            }
            BoundedFormatIterator operator++(int) noexcept
            {
                return *this;
            }

        private:
            std::string *output_ = nullptr;
            std::size_t limit_ = 0;
            std::size_t *written_ = nullptr;
            bool *truncated_ = nullptr;
        };

        /// @brief Formats through the bounded iterator and reports whether output was truncated.
        template <typename Format, typename... Args>
        bool formatBounded(std::string &scratch, std::size_t maxMessageLength, Format format, Args &&...args)
        {
            scratch.clear();
            std::size_t written = 0;
            bool truncated = false;
            BoundedFormatIterator output(scratch, maxMessageLength, written, truncated);
            std::format_to(output, format, std::forward<Args>(args)...);
            if (!truncated)
            {
                return false;
            }
            truncateScratch(scratch, maxMessageLength);
            return true;
        }

        /// @brief Bounded formatter for strings that are only known at runtime.
        template <typename... Args>
        bool runtimeFormatBounded(std::string &scratch, std::size_t maxMessageLength, Types::RuntimeFormat format, Args &...args)
        {
            scratch.clear();
            std::size_t written = 0;
            bool truncated = false;
            BoundedFormatIterator output(scratch, maxMessageLength, written, truncated);
            std::vformat_to(output, format.text, std::make_format_args(args...));
            if (!truncated)
            {
                return false;
            }
            truncateScratch(scratch, maxMessageLength);
            return true;
        }

        /// @brief Applies the active formatting policy before Logger takes ownership of the message text.
        template <typename Format, typename... Args>
        bool formatWithPolicy(std::string &scratch, std::size_t maxMessageLength, Format format, Args &&...args)
        {
            if (getFormatPolicyForFormatting() == Types::FormatPolicy::FastNormal)
            {
                scratch.clear();
                std::format_to(std::back_inserter(scratch), format, std::forward<Args>(args)...);
                return truncateScratchIfNeeded(scratch, maxMessageLength);
            }
            return formatBounded(scratch, maxMessageLength, format, std::forward<Args>(args)...);
        }

        /// @brief Applies the active formatting policy to runtime-provided format strings.
        template <typename... Args>
        bool runtimeFormatWithPolicy(std::string &scratch, std::size_t maxMessageLength, Types::RuntimeFormat format, Args &...args)
        {
            if (getFormatPolicyForFormatting() == Types::FormatPolicy::FastNormal)
            {
                scratch.clear();
                std::vformat_to(std::back_inserter(scratch), format.text, std::make_format_args(args...));
                return truncateScratchIfNeeded(scratch, maxMessageLength);
            }
            return runtimeFormatBounded(scratch, maxMessageLength, format, args...);
        }

        // ------------------------------------------------------------
        // Public template bridges
        // ------------------------------------------------------------

        /// @brief Formats a normal log message and contains all formatting/allocation failures.
        template <typename Source, typename... Args>
        void formatAndLog(Types::Level level, Source source, std::format_string<Args...> format, Args &&...args) noexcept
        {
            try
            {
                FormatScratchLease lease;
                std::string &scratch = lease.text();
                const bool truncated = formatWithPolicy(scratch, getMaxMessageLengthForFormatting(), format, std::forward<Args>(args)...);
                enqueuePreformattedMessage(level, source, scratch, truncated);
            }
            catch (const std::format_error &)
            {
                recordFormatFailure();
            }
            catch (...)
            {
                recordAllocationFailure();
            }
        }

        /// @brief Runtime-format log bridge that avoids moving arguments before std::make_format_args observes them.
        template <typename Source, typename... Args>
        void runtimeFormatAndLog(Types::Level level, Source source, Types::RuntimeFormat format, Args &...args) noexcept
        {
            try
            {
                FormatScratchLease lease;
                std::string &scratch = lease.text();
                const bool truncated = runtimeFormatWithPolicy(scratch, getMaxMessageLengthForFormatting(), format, args...);
                enqueuePreformattedMessage(level, source, scratch, truncated);
            }
            catch (const std::format_error &)
            {
                recordFormatFailure();
            }
            catch (...)
            {
                recordAllocationFailure();
            }
        }

        /// @brief Builds a report result for failures that happen before report delivery begins.
        [[nodiscard]] inline Types::Report::Result reportFailure(IO::Types::ErrorCode code) noexcept
        {
            Types::Report::Result result;
            result.status = IO::makeStatus(code);
            return result;
        }

        /// @brief Formats a synchronous report and maps formatting/allocation failures into the report result.
        template <typename Source, typename... Args>
        Types::Report::Result formatAndReport(
            Types::Level level,
            Source source,
            bool showPopup,
            const std::chrono::milliseconds *timeout,
            std::format_string<Args...> format,
            Args &&...args) noexcept
        {
            try
            {
                FormatScratchLease lease;
                std::string &scratch = lease.text();
                const bool truncated = formatWithPolicy(scratch, getMaxMessageLengthForFormatting(), format, std::forward<Args>(args)...);
                return reportPreformattedMessage(level, normalizeSource(source), scratch, showPopup, truncated, timeout);
            }
            catch (const std::format_error &)
            {
                recordFormatFailure();
                return reportFailure(IO::Types::ErrorCode::InvalidArgument);
            }
            catch (const std::bad_alloc &)
            {
                recordAllocationFailure();
                return reportFailure(IO::Types::ErrorCode::OutOfMemory);
            }
            catch (...)
            {
                recordAllocationFailure();
                return reportFailure(IO::Types::ErrorCode::Unknown);
            }
        }

        /// @brief Runtime-format report bridge that maps formatting failures into the synchronous result.
        template <typename Source, typename... Args>
        Types::Report::Result runtimeFormatAndReport(
            Types::Level level,
            Source source,
            bool showPopup,
            const std::chrono::milliseconds *timeout,
            Types::RuntimeFormat format,
            Args &...args) noexcept
        {
            try
            {
                FormatScratchLease lease;
                std::string &scratch = lease.text();
                const bool truncated = runtimeFormatWithPolicy(scratch, getMaxMessageLengthForFormatting(), format, args...);
                return reportPreformattedMessage(level, normalizeSource(source), scratch, showPopup, truncated, timeout);
            }
            catch (const std::format_error &)
            {
                recordFormatFailure();
                return reportFailure(IO::Types::ErrorCode::InvalidArgument);
            }
            catch (const std::bad_alloc &)
            {
                recordAllocationFailure();
                return reportFailure(IO::Types::ErrorCode::OutOfMemory);
            }
            catch (...)
            {
                recordAllocationFailure();
                return reportFailure(IO::Types::ErrorCode::Unknown);
            }
        }
    } // namespace Detail::Core
    /// @endcond
} // namespace GameWIP::Logger
