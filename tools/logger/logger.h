#pragma once // This header guard prevents the logger from being included twice in the same translation unit.

#include <cstddef>     // For std::size_t.
#include <string_view> // For std::string_view, which reads log messages without copying them.

#if defined(_WIN32) // On Windows, we need to use __declspec to export/import the logger functions.
#if defined(GAMEWIP_LOGGER_BUILD)
#define GAMEWIP_LOGGER_API __declspec(dllexport)
#else
#define GAMEWIP_LOGGER_API __declspec(dllimport)
#endif
#else
#define GAMEWIP_LOGGER_API
#endif

namespace GameWIP
{
    // LogLevel table, using enum to auto assign numbers to the contents.

    /// @brief Determines the severity of a log message.
    enum class LogLevel
    {
        INFO,
        WARN,
        ERR,  // something that the program can recover from, but should be fixed.
        FATAL // something that the program cannot recover from, and should be fixed immediately.
    };

    /// @brief Determines where log messages should be written.
    enum class OutputMode
    {
        NONE,
        CONSOLE,
        FILE,
        BOTH
    };

    class GAMEWIP_LOGGER_API Logger
    {
    public:
        /// @brief Gets the minimum log level.
        /// @return The minimum log level.
        static LogLevel getMinLogLevel();

        /// @brief Gets the current output mode.
        /// @return The current output mode.
        static OutputMode getOutputMode();

        /// @brief Gets the number of dropped log messages.
        /// @return The number of dropped log messages.
        static std::size_t getDroppedLogCount();

        /// @brief Initializes the logger.
        /// @param newMode The output mode.
        /// @param newLevel The minimum log level.
        /// @param maxQueueSize The maximum size of the log queue.
        static void init(OutputMode newMode = OutputMode::BOTH, LogLevel newLevel = LogLevel::INFO, std::size_t maxQueueSize = 1024);

        // std::string_view reads message without forcing an extra std::string allocation on every call.

        /// @brief Logs a message.
        /// @param level The log level of the message.
        /// @param source The source tag for the message.
        /// @param message The log message to be written.
        static void log(LogLevel level, std::string_view source, std::string_view message);

        /// @brief Logs a message to the platform debug output.
        /// @param level The log level of the message.
        /// @param source The source tag for the message.
        /// @param message The log message to be written.
        static void logDebugOutput(LogLevel level, std::string_view source, std::string_view message);

        /// @brief Displays a fatal error popup with the specified message.
        /// @param message The error message to display.
        static void showFatalPopup(std::string_view message);

        /// @brief Flushes the log file and console streams to ensure all log messages are written out.
        static void flush();

        /// @brief Shuts down the logger, ensuring all log messages are processed and resources are cleaned up.
        static void shutdown();
    };
}
