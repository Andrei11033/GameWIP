/// @file reports.h
/// @brief Public synchronous diagnostic reporting APIs.

#pragma once

#include "logger/config.h"
#include "logger/detail/report_templates.h"
#include "logger/logger_export.h"
#include "logger/message_format.h"
#include "logger/record.h"

#include <chrono>
#include <exception>
#include <format>
#include <optional>
#include <string_view>
#include <utility>

namespace GameWIP::Logger
{
    // ------------------------------------------------------------
    // Synchronous reporting
    // ------------------------------------------------------------

    /// @name Synchronous reporting
    /// @{

    /// @brief Synchronously reports a preformatted diagnostic through eligible sinks and emergency channels.
    LOGGER_EXPORT Types::Report::Result report(Types::Level level, std::string_view source, std::string_view message) noexcept;
    /// @brief Synchronously reports a preformatted diagnostic under a bounded deadline.
    LOGGER_EXPORT Types::Report::Result report(
        Types::Level level,
        std::string_view source,
        std::chrono::milliseconds timeout,
        std::string_view message) noexcept;
    /// @brief Synchronously reports a preformatted diagnostic with a registered source.
    LOGGER_EXPORT Types::Report::Result report(Types::Level level, Types::SourceId source, std::string_view message) noexcept;
    /// @brief Synchronously reports a registered-source diagnostic under a bounded deadline.
    LOGGER_EXPORT Types::Report::Result report(
        Types::Level level,
        Types::SourceId source,
        std::chrono::milliseconds timeout,
        std::string_view message) noexcept;

    /// @brief Synchronously reports a preformatted diagnostic with an enum source.
    template <typename Source>
        requires(SourceEnum<Source>)
    Types::Report::Result report(Types::Level level, Source source, std::string_view message) noexcept
    {
        return report(level, Detail::Core::sourceId(source), message);
    }

    /// @brief Synchronously reports an enum-source diagnostic under a bounded deadline.
    template <typename Source>
        requires(SourceEnum<Source>)
    Types::Report::Result report(Types::Level level, Source source, std::chrono::milliseconds timeout, std::string_view message) noexcept
    {
        return report(level, Detail::Core::sourceId(source), timeout, message);
    }

    /// @brief Formats and synchronously reports a diagnostic.
    template <typename Source, typename... Args>
        requires(sizeof...(Args) > 0)
    Types::Report::Result report(Types::Level level, Source source, std::format_string<Args...> format, Args &&...args) noexcept
    {
        return Detail::Core::formatAndReport(level, source, false, nullptr, format, std::forward<Args>(args)...);
    }

    /// @brief Synchronously reports a preformatted diagnostic with fields and metadata.
    template <typename Source>
    Types::Report::Result report(Types::Level level, Source source, const Record &record, std::string_view message) noexcept
    {
        return Detail::Core::reportPreformattedWithRecord(level, source, record, false, nullptr, message);
    }

    /// @brief Synchronously reports a preformatted diagnostic with a deadline, fields, and metadata.
    template <typename Source>
    Types::Report::Result report(
        Types::Level level,
        Source source,
        const Record &record,
        std::chrono::milliseconds timeout,
        std::string_view message) noexcept
    {
        return Detail::Core::reportPreformattedWithRecord(level, source, record, false, &timeout, message);
    }

    /// @brief Formats and synchronously reports a diagnostic with fields and metadata.
    template <typename Source, typename... Args>
        requires(sizeof...(Args) > 0)
    Types::Report::Result report(Types::Level level, Source source, const Record &record, std::format_string<Args...> format, Args &&...args) noexcept
    {
        return Detail::Core::formatAndReportWithRecord(level, source, record, false, nullptr, format, std::forward<Args>(args)...);
    }

    /// @brief Formats and synchronously reports a diagnostic with a deadline, fields, and metadata.
    template <typename Source, typename... Args>
        requires(sizeof...(Args) > 0)
    Types::Report::Result report(
        Types::Level level,
        Source source,
        const Record &record,
        std::chrono::milliseconds timeout,
        std::format_string<Args...> format,
        Args &&...args) noexcept
    {
        return Detail::Core::formatAndReportWithRecord(level, source, record, false, &timeout, format, std::forward<Args>(args)...);
    }

    /// @brief Runtime-formats and synchronously reports a diagnostic with fields and metadata.
    template <typename Source, typename... Args>
        requires(sizeof...(Args) > 0)
    Types::Report::Result report(Types::Level level, Source source, const Record &record, Types::RuntimeFormat format, Args &&...args) noexcept
    {
        return Detail::Core::runtimeFormatAndReportWithRecord(level, source, record, false, nullptr, format, args...);
    }

    /// @brief Runtime-formats and synchronously reports a diagnostic with a deadline, fields, and metadata.
    template <typename Source, typename... Args>
        requires(sizeof...(Args) > 0)
    Types::Report::Result report(
        Types::Level level,
        Source source,
        const Record &record,
        std::chrono::milliseconds timeout,
        Types::RuntimeFormat format,
        Args &&...args) noexcept
    {
        return Detail::Core::runtimeFormatAndReportWithRecord(level, source, record, false, &timeout, format, args...);
    }

    /// @brief Formats and synchronously reports a diagnostic under a bounded deadline.
    template <typename Source, typename... Args>
        requires(sizeof...(Args) > 0)
    Types::Report::Result report(
        Types::Level level,
        Source source,
        std::chrono::milliseconds timeout,
        std::format_string<Args...> format,
        Args &&...args) noexcept
    {
        return Detail::Core::formatAndReport(level, source, false, &timeout, format, std::forward<Args>(args)...);
    }

    /// @brief Runtime-formats and synchronously reports a diagnostic.
    template <typename Source, typename... Args>
        requires(sizeof...(Args) > 0)
    Types::Report::Result report(Types::Level level, Source source, Types::RuntimeFormat format, Args &&...args) noexcept
    {
        return Detail::Core::runtimeFormatAndReport(level, source, false, nullptr, format, args...);
    }

    /// @brief Runtime-formats and synchronously reports a diagnostic under a bounded deadline.
    template <typename Source, typename... Args>
        requires(sizeof...(Args) > 0)
    Types::Report::Result report(
        Types::Level level,
        Source source,
        std::chrono::milliseconds timeout,
        Types::RuntimeFormat format,
        Args &&...args) noexcept
    {
        return Detail::Core::runtimeFormatAndReport(level, source, false, &timeout, format, args...);
    }

    /// @brief Synchronously reports a preformatted Error with a UTF-8 string source.
    LOGGER_EXPORT Types::Report::Result reportError(std::string_view source, std::string_view message) noexcept;
    /// @brief Synchronously reports a preformatted Error under a bounded deadline.
    LOGGER_EXPORT Types::Report::Result reportError(std::string_view source, std::chrono::milliseconds timeout, std::string_view message) noexcept;
    /// @brief Synchronously reports a preformatted Error with a registered source.
    LOGGER_EXPORT Types::Report::Result reportError(Types::SourceId source, std::string_view message) noexcept;
    /// @brief Synchronously reports a registered-source Error under a bounded deadline.
    LOGGER_EXPORT Types::Report::Result reportError(Types::SourceId source, std::chrono::milliseconds timeout, std::string_view message) noexcept;

    /// @brief Synchronously reports a preformatted Fatal diagnostic and optional popup.
    LOGGER_EXPORT Types::Report::Result reportFatal(std::string_view source, std::string_view message) noexcept;
    /// @brief Synchronously reports a preformatted Fatal diagnostic under a bounded deadline.
    LOGGER_EXPORT Types::Report::Result reportFatal(std::string_view source, std::chrono::milliseconds timeout, std::string_view message) noexcept;
    /// @brief Synchronously reports a registered-source Fatal diagnostic and optional popup.
    LOGGER_EXPORT Types::Report::Result reportFatal(Types::SourceId source, std::string_view message) noexcept;
    /// @brief Synchronously reports a registered-source Fatal diagnostic under a bounded deadline.
    LOGGER_EXPORT Types::Report::Result reportFatal(Types::SourceId source, std::chrono::milliseconds timeout, std::string_view message) noexcept;

    /// @brief Synchronously reports a preformatted Error with fields and metadata.
    template <typename Source> Types::Report::Result reportError(Source source, const Record &record, std::string_view message) noexcept
    {
        return report(Types::Level::Error, source, record, message);
    }

    /// @brief Synchronously reports a preformatted Error with a deadline, fields, and metadata.
    template <typename Source>
    Types::Report::Result reportError(Source source, const Record &record, std::chrono::milliseconds timeout, std::string_view message) noexcept
    {
        return report(Types::Level::Error, source, record, timeout, message);
    }

    /// @brief Synchronously reports a preformatted Fatal diagnostic with fields and metadata.
    template <typename Source> Types::Report::Result reportFatal(Source source, const Record &record, std::string_view message) noexcept
    {
        return Detail::Core::reportPreformattedWithRecord(Types::Level::Fatal, source, record, true, nullptr, message);
    }

    /// @brief Synchronously reports a preformatted Fatal diagnostic with a deadline, fields, and metadata.
    template <typename Source>
    Types::Report::Result reportFatal(Source source, const Record &record, std::chrono::milliseconds timeout, std::string_view message) noexcept
    {
        return Detail::Core::reportPreformattedWithRecord(Types::Level::Fatal, source, record, true, &timeout, message);
    }

    /// @brief Formats and synchronously reports an Error with fields and metadata.
    template <typename Source, typename... Args>
        requires(sizeof...(Args) > 0)
    Types::Report::Result reportError(Source source, const Record &record, std::format_string<Args...> format, Args &&...args) noexcept
    {
        return report(Types::Level::Error, source, record, format, std::forward<Args>(args)...);
    }

    /// @brief Formats and synchronously reports an Error with a deadline, fields, and metadata.
    template <typename Source, typename... Args>
        requires(sizeof...(Args) > 0)
    Types::Report::Result reportError(
        Source source,
        const Record &record,
        std::chrono::milliseconds timeout,
        std::format_string<Args...> format,
        Args &&...args) noexcept
    {
        return report(Types::Level::Error, source, record, timeout, format, std::forward<Args>(args)...);
    }

    /// @brief Formats and synchronously reports a Fatal diagnostic with fields and metadata.
    template <typename Source, typename... Args>
        requires(sizeof...(Args) > 0)
    Types::Report::Result reportFatal(Source source, const Record &record, std::format_string<Args...> format, Args &&...args) noexcept
    {
        return Detail::Core::formatAndReportWithRecord(Types::Level::Fatal, source, record, true, nullptr, format, std::forward<Args>(args)...);
    }

    /// @brief Formats and synchronously reports a Fatal diagnostic with a deadline, fields, and metadata.
    template <typename Source, typename... Args>
        requires(sizeof...(Args) > 0)
    Types::Report::Result reportFatal(
        Source source,
        const Record &record,
        std::chrono::milliseconds timeout,
        std::format_string<Args...> format,
        Args &&...args) noexcept
    {
        return Detail::Core::formatAndReportWithRecord(Types::Level::Fatal, source, record, true, &timeout, format, std::forward<Args>(args)...);
    }

    /// @brief Runtime-formats and synchronously reports an Error with fields and metadata.
    template <typename Source, typename... Args>
        requires(sizeof...(Args) > 0)
    Types::Report::Result reportError(Source source, const Record &record, Types::RuntimeFormat format, Args &&...args) noexcept
    {
        return Detail::Core::runtimeFormatAndReportWithRecord(Types::Level::Error, source, record, false, nullptr, format, args...);
    }

    /// @brief Runtime-formats and synchronously reports an Error with a deadline, fields, and metadata.
    template <typename Source, typename... Args>
        requires(sizeof...(Args) > 0)
    Types::Report::Result reportError(
        Source source,
        const Record &record,
        std::chrono::milliseconds timeout,
        Types::RuntimeFormat format,
        Args &&...args) noexcept
    {
        return Detail::Core::runtimeFormatAndReportWithRecord(Types::Level::Error, source, record, false, &timeout, format, args...);
    }

    /// @brief Runtime-formats and synchronously reports a Fatal diagnostic with fields and metadata.
    template <typename Source, typename... Args>
        requires(sizeof...(Args) > 0)
    Types::Report::Result reportFatal(Source source, const Record &record, Types::RuntimeFormat format, Args &&...args) noexcept
    {
        return Detail::Core::runtimeFormatAndReportWithRecord(Types::Level::Fatal, source, record, true, nullptr, format, args...);
    }

    /// @brief Runtime-formats and synchronously reports a Fatal diagnostic with a deadline, fields, and metadata.
    template <typename Source, typename... Args>
        requires(sizeof...(Args) > 0)
    Types::Report::Result reportFatal(
        Source source,
        const Record &record,
        std::chrono::milliseconds timeout,
        Types::RuntimeFormat format,
        Args &&...args) noexcept
    {
        return Detail::Core::runtimeFormatAndReportWithRecord(Types::Level::Fatal, source, record, true, &timeout, format, args...);
    }

    /// @brief Synchronously reports a preformatted Error with an enum source.
    template <typename Source>
        requires(SourceEnum<Source>)
    Types::Report::Result reportError(Source source, std::string_view message) noexcept
    {
        return reportError(Detail::Core::sourceId(source), message);
    }

    /// @brief Synchronously reports an enum-source Error under a bounded deadline.
    template <typename Source>
        requires(SourceEnum<Source>)
    Types::Report::Result reportError(Source source, std::chrono::milliseconds timeout, std::string_view message) noexcept
    {
        return reportError(Detail::Core::sourceId(source), timeout, message);
    }

    /// @brief Synchronously reports a preformatted Fatal diagnostic with an enum source.
    template <typename Source>
        requires(SourceEnum<Source>)
    Types::Report::Result reportFatal(Source source, std::string_view message) noexcept
    {
        return reportFatal(Detail::Core::sourceId(source), message);
    }

    /// @brief Synchronously reports an enum-source Fatal diagnostic under a bounded deadline.
    template <typename Source>
        requires(SourceEnum<Source>)
    Types::Report::Result reportFatal(Source source, std::chrono::milliseconds timeout, std::string_view message) noexcept
    {
        return reportFatal(Detail::Core::sourceId(source), timeout, message);
    }

    /// @brief Declares formatted fixed-severity report overloads.
#define LOGGER_DECLARE_FORMATTED_REPORT(name, levelValue, popupValue) \
    template <typename Source, typename... Args> \
        requires(sizeof...(Args) > 0) \
    Types::Report::Result name(Source source, std::format_string<Args...> format, Args &&...args) noexcept \
    { \
        return Detail::Core::formatAndReport(levelValue, source, popupValue, nullptr, format, std::forward<Args>(args)...); \
    } \
    template <typename Source, typename... Args> \
        requires(sizeof...(Args) > 0) \
    Types::Report::Result name(Source source, std::chrono::milliseconds timeout, std::format_string<Args...> format, Args &&...args) noexcept \
    { \
        return Detail::Core::formatAndReport(levelValue, source, popupValue, &timeout, format, std::forward<Args>(args)...); \
    } \
    template <typename Source, typename... Args> \
        requires(sizeof...(Args) > 0) \
    Types::Report::Result name(Source source, Types::RuntimeFormat format, Args &&...args) noexcept \
    { \
        return Detail::Core::runtimeFormatAndReport(levelValue, source, popupValue, nullptr, format, args...); \
    } \
    template <typename Source, typename... Args> \
        requires(sizeof...(Args) > 0) \
    Types::Report::Result name(Source source, std::chrono::milliseconds timeout, Types::RuntimeFormat format, Args &&...args) noexcept \
    { \
        return Detail::Core::runtimeFormatAndReport(levelValue, source, popupValue, &timeout, format, args...); \
    }

    LOGGER_DECLARE_FORMATTED_REPORT(reportError, Types::Level::Error, false)
    LOGGER_DECLARE_FORMATTED_REPORT(reportFatal, Types::Level::Fatal, true)
#undef LOGGER_DECLARE_FORMATTED_REPORT

    /// @}

    // ------------------------------------------------------------
    // Fatal termination
    // ------------------------------------------------------------

    /// @name Fatal termination
    /// @{

    /// @brief Reports a Fatal diagnostic and then calls std::terminate().
    [[noreturn]] LOGGER_EXPORT void fatalTerminate(std::string_view source, std::string_view message) noexcept;
    /// @brief Reports a registered-source Fatal diagnostic and then calls std::terminate().
    [[noreturn]] LOGGER_EXPORT void fatalTerminate(Types::SourceId source, std::string_view message) noexcept;
    /// @brief Reports a Fatal diagnostic under a bounded deadline and then calls std::terminate().
    [[noreturn]] LOGGER_EXPORT void fatalTerminate(std::string_view source, std::chrono::milliseconds timeout, std::string_view message) noexcept;
    /// @brief Reports a registered-source Fatal diagnostic under a bounded deadline and then calls std::terminate().
    [[noreturn]] LOGGER_EXPORT void fatalTerminate(Types::SourceId source, std::chrono::milliseconds timeout, std::string_view message) noexcept;

    /// @brief Reports an enum-source Fatal diagnostic and then calls std::terminate().
    template <typename Source>
        requires(SourceEnum<Source>)
    [[noreturn]] void fatalTerminate(Source source, std::string_view message) noexcept
    {
        fatalTerminate(Detail::Core::sourceId(source), message);
    }

    /// @brief Reports an enum-source Fatal diagnostic under a deadline and then terminates.
    template <typename Source>
        requires(SourceEnum<Source>)
    [[noreturn]] void fatalTerminate(Source source, std::chrono::milliseconds timeout, std::string_view message) noexcept
    {
        fatalTerminate(Detail::Core::sourceId(source), timeout, message);
    }

    /// @brief Formats a Fatal diagnostic, reports it, and then calls std::terminate().
    template <typename Source, typename... Args>
        requires(sizeof...(Args) > 0)
    [[noreturn]] void fatalTerminate(Source source, std::format_string<Args...> format, Args &&...args) noexcept
    {
        static_cast<void>(Detail::Core::formatAndReport(Types::Level::Fatal, source, true, nullptr, format, std::forward<Args>(args)...));
        std::terminate();
    }

    /// @brief Formats and reports a Fatal diagnostic under a deadline, then terminates.
    template <typename Source, typename... Args>
        requires(sizeof...(Args) > 0)
    [[noreturn]] void fatalTerminate(Source source, std::chrono::milliseconds timeout, std::format_string<Args...> format, Args &&...args) noexcept
    {
        static_cast<void>(Detail::Core::formatAndReport(Types::Level::Fatal, source, true, &timeout, format, std::forward<Args>(args)...));
        std::terminate();
    }

    /// @brief Runtime-formats a Fatal diagnostic, reports it, and then terminates.
    template <typename Source, typename... Args>
        requires(sizeof...(Args) > 0)
    [[noreturn]] void fatalTerminate(Source source, Types::RuntimeFormat format, Args &&...args) noexcept
    {
        static_cast<void>(Detail::Core::runtimeFormatAndReport(Types::Level::Fatal, source, true, nullptr, format, args...));
        std::terminate();
    }

    /// @brief Runtime-formats and reports a Fatal diagnostic under a deadline, then terminates.
    template <typename Source, typename... Args>
        requires(sizeof...(Args) > 0)
    [[noreturn]] void fatalTerminate(Source source, std::chrono::milliseconds timeout, Types::RuntimeFormat format, Args &&...args) noexcept
    {
        static_cast<void>(Detail::Core::runtimeFormatAndReport(Types::Level::Fatal, source, true, &timeout, format, args...));
        std::terminate();
    }

    /// @}

    // ------------------------------------------------------------
    // Debugger output
    // ------------------------------------------------------------

    /// @name Debugger output
    /// @{

    /// @brief Writes one validated UTF-8 diagnostic directly to the debugger channel.
    LOGGER_EXPORT IO::Types::Status writeDebugOutput(Types::Level level, std::string_view source, std::string_view message) noexcept;

    /// @}
} // namespace GameWIP::Logger
