#pragma once

#include "logger/logger.h"

/// @file logger_macros.h
/// @brief Optional lazy convenience macros for the GameWIP Logger.
///
/// Include this header only when the global `LOGGER_*` preprocessor shortcuts are wanted.
/// The macros call `GameWIP::Logger::shouldLog(...)` before evaluating message/format
/// arguments, so filtered log calls avoid expensive formatting and argument construction.
/// The class API in `logger/logger.h` remains the primary public API.

/// @def LOGGER_DETAIL(levelValue, callName, sourceValue, ...)
/// @brief Internal macro body shared by the public `LOGGER_*` macros.
/// @details Captures the source expression once and evaluates message/format arguments only
/// after `Logger::shouldLog(...)` passes. Filtered logs are intentional skips and do not
/// affect dropped-log counters.
#define LOGGER_DETAIL(levelValue, callName, sourceValue, ...)           \
    do                                                                  \
    {                                                                   \
        auto &&loggerSource__ = (sourceValue);                          \
        if (::GameWIP::Logger::shouldLog((levelValue), loggerSource__)) \
        {                                                               \
            ::GameWIP::Logger::callName(loggerSource__, __VA_ARGS__);   \
        }                                                               \
    } while (false)

/// @def LOGGER_TRACE(source, ...)
/// @brief Lazy Trace log macro.
/// @param source String source, registered `SourceId`, or unsigned enum source.
/// @param ... Message text or format string plus arguments.
/// @details Compiles out in release-style builds unless `LOGGER_ENABLE_TRACE_LOGS` is defined.
#if !defined(NDEBUG) || defined(LOGGER_ENABLE_TRACE_LOGS)
#define LOGGER_TRACE(source, ...) LOGGER_DETAIL(::GameWIP::Logger::Types::Level::Trace, trace, source, __VA_ARGS__)
#else
#define LOGGER_TRACE(...) ((void)0)
#endif

/// @def LOGGER_DEBUG(source, ...)
/// @brief Lazy Debug log macro.
/// @param source String source, registered `SourceId`, or unsigned enum source.
/// @param ... Message text or format string plus arguments.
/// @details Compiles out in release-style builds unless `LOGGER_ENABLE_DEBUG_LOGS` is defined.
#if !defined(NDEBUG) || defined(LOGGER_ENABLE_DEBUG_LOGS)
#define LOGGER_DEBUG(source, ...) LOGGER_DETAIL(::GameWIP::Logger::Types::Level::Debug, debug, source, __VA_ARGS__)
#else
#define LOGGER_DEBUG(...) ((void)0)
#endif

/// @def LOGGER_INFO(source, ...)
/// @brief Lazy Info log macro.
/// @param source String source, registered `SourceId`, or unsigned enum source.
/// @param ... Message text or format string plus arguments.
#define LOGGER_INFO(source, ...) LOGGER_DETAIL(::GameWIP::Logger::Types::Level::Info, info, source, __VA_ARGS__)

/// @def LOGGER_WARN(source, ...)
/// @brief Lazy Warn log macro.
/// @param source String source, registered `SourceId`, or unsigned enum source.
/// @param ... Message text or format string plus arguments.
#define LOGGER_WARN(source, ...) LOGGER_DETAIL(::GameWIP::Logger::Types::Level::Warn, warn, source, __VA_ARGS__)

/// @def LOGGER_ERROR(source, ...)
/// @brief Lazy Error log macro.
/// @param source String source, registered `SourceId`, or unsigned enum source.
/// @param ... Message text or format string plus arguments.
#define LOGGER_ERROR(source, ...) LOGGER_DETAIL(::GameWIP::Logger::Types::Level::Error, error, source, __VA_ARGS__)

/// @def LOGGER_FATAL(source, ...)
/// @brief Lazy Fatal-severity normal log macro.
/// @param source String source, registered `SourceId`, or unsigned enum source.
/// @param ... Message text or format string plus arguments.
/// @details This is still a normal async log. It does not terminate and does not force a popup.
#define LOGGER_FATAL(source, ...) LOGGER_DETAIL(::GameWIP::Logger::Types::Level::Fatal, fatal, source, __VA_ARGS__)

/// @def LOGGER_FATAL_TERMINATE(source, ...)
/// @brief Reports Fatal synchronously, flushes, may show the fatal popup, and terminates.
/// @param source String source, registered `SourceId`, or unsigned enum source.
/// @param ... Message text or format string plus arguments.
#define LOGGER_FATAL_TERMINATE(source, ...)                     \
    do                                                          \
    {                                                           \
        ::GameWIP::Logger::fatalTerminate(source, __VA_ARGS__); \
    } while (false)
