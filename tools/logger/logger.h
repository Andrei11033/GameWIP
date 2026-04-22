#pragma once           // This header guard prevents the logger from being included twice in the same translation unit.
#include <string_view> // For std::string_view, which reads log messages without copying them.

#if defined(_WIN32)
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
    // This is used to determine the severity of the logs.
    enum class LogLevel
    {
        INFO,
        WARN,
        ERR,  // something that the program can recover from, but should be fixed.
        FATAL // something that the program cannot recover from, and should be fixed immediately.
    };

    // This is used to determine where the logs should be outputted.
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
        // Setter for the log level.
        static void setLogLevel(LogLevel level);

        // A function that returns the minimum log level.
        static LogLevel getMinLogLevel();

        // A function that returns the current output mode.
        static OutputMode getOutputMode();

        // A function to initialize the logger.
        static void init(OutputMode newMode = OutputMode::BOTH, LogLevel newLevel = LogLevel::INFO);

        // The log function, can be called to log messages.
        // std::string_view reads message without forcing an extra std::string allocation on every call.
        static void log(LogLevel level, std::string_view source, std::string_view message);

        // A function to log messages to the Windows debug output.
        static void logDBWIN(LogLevel level, std::string_view source, std::string_view message);

        // A function to show a fatal error message in a pop-up dialog box.
        static void fatalPopUp(std::string_view message);

        // A function to flush the log file.
        static void flush();

        // A function to shutdown the logger.
        static void shutdown();
    };
}