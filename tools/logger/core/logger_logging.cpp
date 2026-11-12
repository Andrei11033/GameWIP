/// @file logger_logging.cpp
/// @brief Queue submission and exported asynchronous logging functions.

#include "logger/internal/logger_core.h"

using namespace GameWIP::Logger::Detail::Core;

// ------------------------------------------------------------
// Queue submission
// ------------------------------------------------------------

void GameWIP::Logger::Detail::Core::enqueuePreformattedMessage(LogLevel level, std::string_view source, std::string_view message) noexcept
{
    enqueuePreformattedMessage(level, source, message, false);
}

void GameWIP::Logger::Detail::Core::enqueuePreformattedMessage(
    LogLevel level,
    std::string_view source,
    std::string_view message,
    bool truncated) noexcept
{
    try
    {
        enqueueAndWakeWorker(makePendingEntry(level, source, message, truncated));
    }
    catch(...)
    {
        countAllocationFailure();
    }
}

void GameWIP::Logger::Detail::Core::enqueuePreformattedMessage(LogLevel level, SourceId source, std::string_view message) noexcept
{
    enqueuePreformattedMessage(level, source, message, false);
}

void GameWIP::Logger::Detail::Core::enqueuePreformattedMessage(LogLevel level, SourceId source, std::string_view message, bool truncated) noexcept
{
    try
    {
        enqueueAndWakeWorker(makePendingEntry(level, source, message, truncated));
    }
    catch(...)
    {
        countAllocationFailure();
    }
}

// ------------------------------------------------------------
// Asynchronous logging
// ------------------------------------------------------------

void GameWIP::Logger::log(LogLevel level, std::string_view source, std::string_view message) noexcept
{
    if(!shouldLog(level))
    {
        return;
    }

    try
    {
        enqueueAndWakeWorker(makePendingEntry(level, source, message));
    }
    catch(...)
    {
        countAllocationFailure();
    }
}

void GameWIP::Logger::log(LogLevel level, SourceId source, std::string_view message) noexcept
{
    if(!shouldLog(level, source))
    {
        return;
    }

    try
    {
        enqueueAndWakeWorker(makePendingEntry(level, source, message));
    }
    catch(...)
    {
        countAllocationFailure();
    }
}

#define LOGGER_DEFINE_LEVEL(name, levelValue) \
    void GameWIP::Logger::name(std::string_view source, std::string_view message) noexcept \
    { \
        log(levelValue, source, message); \
    } \
    void GameWIP::Logger::name(SourceId source, std::string_view message) noexcept \
    { \
        log(levelValue, source, message); \
    } \
    void GameWIP::Logger::name(std::string_view source, const Record &record, std::string_view message) noexcept \
    { \
        try \
        { \
            if(shouldLog(levelValue, source)) \
            { \
                preformattedLogWithRecord(levelValue, source, record, message); \
            } \
        } \
        catch(...) \
        { \
            recordAllocationFailure(); \
        } \
    } \
    void GameWIP::Logger::name(SourceId source, const Record &record, std::string_view message) noexcept \
    { \
        try \
        { \
            if(shouldLog(levelValue, source)) \
            { \
                preformattedLogWithRecord(levelValue, source, record, message); \
            } \
        } \
        catch(...) \
        { \
            recordAllocationFailure(); \
        } \
    }
LOGGER_DEFINE_LEVEL(trace, LogLevel::Trace)
LOGGER_DEFINE_LEVEL(debug, LogLevel::Debug)
LOGGER_DEFINE_LEVEL(info, LogLevel::Info)
LOGGER_DEFINE_LEVEL(warn, LogLevel::Warn)
LOGGER_DEFINE_LEVEL(error, LogLevel::Error)
LOGGER_DEFINE_LEVEL(fatal, LogLevel::Fatal)
#undef LOGGER_DEFINE_LEVEL
