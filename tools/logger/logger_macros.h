#pragma once

#include "logger/logger.h"

/// @brief Lazy logger macro body.
/// @details Captures the source expression once and evaluates message/format arguments only after
/// Logger::shouldLog() passes. Filtered logs are intentional skips and do not affect dropped-log counters.
#define LOGGER_DETAIL(levelValue, callName, sourceValue, ...)           \
    do                                                                  \
    {                                                                   \
        auto &&loggerSource__ = (sourceValue);                          \
        if (::GameWIP::Logger::shouldLog((levelValue), loggerSource__)) \
        {                                                               \
            ::GameWIP::Logger::callName(loggerSource__, __VA_ARGS__);   \
        }                                                               \
    } while (false)

/// @brief Compile out Trace logs in release-style builds unless LOGGER_ENABLE_TRACE_LOGS is defined.
#if !defined(NDEBUG) || defined(LOGGER_ENABLE_TRACE_LOGS)
#define LOGGER_TRACE(source, ...) LOGGER_DETAIL(::GameWIP::Logger::Types::Level::Trace, trace, source, __VA_ARGS__)
#else
#define LOGGER_TRACE(...) ((void)0)
#endif

/// @brief Compile out Debug logs in release-style builds unless LOGGER_ENABLE_DEBUG_LOGS is defined.
#if !defined(NDEBUG) || defined(LOGGER_ENABLE_DEBUG_LOGS)
#define LOGGER_DEBUG(source, ...) LOGGER_DETAIL(::GameWIP::Logger::Types::Level::Debug, debug, source, __VA_ARGS__)
#else
#define LOGGER_DEBUG(...) ((void)0)
#endif

/// @brief Lazy Info logging. Message/format arguments are evaluated only when current filters allow Info.
#define LOGGER_INFO(source, ...) LOGGER_DETAIL(::GameWIP::Logger::Types::Level::Info, info, source, __VA_ARGS__)
/// @brief Lazy Warn logging. Message/format arguments are evaluated only when current filters allow Warn.
#define LOGGER_WARN(source, ...) LOGGER_DETAIL(::GameWIP::Logger::Types::Level::Warn, warn, source, __VA_ARGS__)
/// @brief Lazy Error logging. Message/format arguments are evaluated only when current filters allow Error.
#define LOGGER_ERROR(source, ...) LOGGER_DETAIL(::GameWIP::Logger::Types::Level::Error, error, source, __VA_ARGS__)
/// @brief Lazy Fatal logging. Message/format arguments are evaluated only when current filters allow Fatal. Does not terminate the process.
#define LOGGER_FATAL(source, ...) LOGGER_DETAIL(::GameWIP::Logger::Types::Level::Fatal, fatal, source, __VA_ARGS__)

/// @brief Logs Fatal, flushes, shows the fatal popup when enabled, then terminates the process.
#define LOGGER_FATAL_TERMINATE(source, ...)                     \
    do                                                          \
    {                                                           \
        ::GameWIP::Logger::fatalTerminate(source, __VA_ARGS__); \
    } while (false)
