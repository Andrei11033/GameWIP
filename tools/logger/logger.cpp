#include "logger.h"   // Include the corresponding header file for the Logger class.
#include <iostream>   // For console output (std::cout and std::cerr).
#include <iomanip>    // For formatting the time output (std::put_time).
#include <sstream>    // For building the time string (std::ostringstream).
#include <fstream>    // For file output (std::ofstream).
#include <filesystem> // For creating the logs directory (std::filesystem::create_directory).
#include <chrono>     // For getting the current time (std::chrono::system_clock).
#include <ctime>      // For converting time to a struct (std::localtime).
#include <format>     // For formatting the final log message (std::format).
#include <string>     // For std::string used by file paths and formatted log messages.
#include <string_view>
#include <windows.h> // For Windows-specific functions like OutputDebugString and MessageBox.

using GameWIP::LogLevel;
using GameWIP::OutputMode;

// Anonymous namespace, used to contain internal vars.
namespace
{
    // Internal logger variables.
    LogLevel level = LogLevel::INFO;    // The minimum log level to output.
    OutputMode mode = OutputMode::BOTH; // The output mode, determines where the logs will be outputted (console, file, both, or none).
    std::ofstream logFile;              // The active log file stream used when file output is enabled.
    std::string logFilePath;            // The full path to the current log file.

    // A helper function to get the current time as a string.
    // The format string is a C-style string because std::put_time expects that.
    std::string getCurTime(const char *timeFormat = "%H:%M:%S")
    {
        auto now = std::chrono::system_clock::now();                    // get the current time, from "Unix epoch".
        std::time_t time_s = std::chrono::system_clock::to_time_t(now); // convert to time_t, round to nearest second.

        // std::localtime(&time_s) converts time_s to a struct containing local hour, minutes, seconds, etc.
        // std::put_time(std::localtime(&time_s), "%H:%M:%S") formats to a string of pattern "Hour:Min:Sec:".
        std::ostringstream ss;                                    // creates the output-only stringstream.
        ss << std::put_time(std::localtime(&time_s), timeFormat); // Streams the formated time into ss(builds the string).
        return ss.str();                                          // gets the built string form ss and returns it.
    }
}

void GameWIP::Logger::setLogLevel(LogLevel newLevel)
{
    level = newLevel;
}

LogLevel GameWIP::Logger::getMinLogLevel()
{
    return level;
}

OutputMode GameWIP::Logger::getOutputMode()
{
    return mode;
}

void GameWIP::Logger::init(OutputMode newMode, LogLevel newLevel)
{
    if (logFile.is_open()) // If a log file is already open, close it before creating a new one.
    {
        logFile.close();
    }

    mode = newMode;      // Store the new output mode.
    level = newLevel;    // Store the new minimum log level.
    logFilePath.clear(); // Clear the old file path when re-initializing.

    if (mode == OutputMode::CONSOLE || mode == OutputMode::NONE)
    {
        return; // If the output mode is CONSOLE or NONE, there's no need to create a log file.
    }

    std::filesystem::create_directory("logs"); // Create the logs directory if it doesn't exist.

    std::string curTime = getCurTime("%Y-%m-%d_%H-%M-%S"); // Get the current time in a format suitable for filenames.
    logFilePath = "logs/" + curTime + ".log";              // Create the full path for the new log file.

    logFile.open(logFilePath); // Open the log file for writing.

    if (!logFile.is_open()) // If the log file failed to open, fallback to console output and log a warning message.
    {
        mode = OutputMode::CONSOLE;
        log(LogLevel::ERR, "Logger Init", "Failed to open log file, falling back to console output.");
        return;
    }
}

void GameWIP::Logger::flush()
{
    std::cout.flush();
    std::cerr.flush();

    if (logFile.is_open())
    {
        logFile.flush();
    }
}

void GameWIP::Logger::shutdown()
{
    flush();

    if (logFile.is_open())
    {
        logFile.close();
    }

    logFilePath.clear();
    mode = OutputMode::NONE;
}

void GameWIP::Logger::log(LogLevel level, std::string_view source, std::string_view message)
{
    if (level < getMinLogLevel() || mode == OutputMode::NONE)
    {
        return;
    }

    bool consoleOutput = (mode == OutputMode::CONSOLE || mode == OutputMode::BOTH);                  // Determine if console output is enabled based on the current output mode.
    bool fileOutput = ((mode == OutputMode::FILE || mode == OutputMode::BOTH) && logFile.is_open()); // Determine if file output is enabled based on the current output mode.
    if (!consoleOutput && !fileOutput)
    {
        return;
    }

    const char *levelStr = "UNKNOWN"; // String literals are cheaper than building std::string objects for fixed text like log level names.
    const char *color = "";
    bool useCerr = true;

    switch (level)
    {
    default: // Default case for unknown log levels.
        break;
    case LogLevel::INFO:
        levelStr = "INFO";
        useCerr = false;
        break;
    case LogLevel::WARN:
        levelStr = "WARN";
        color = "\033[33m"; // Yellow color
        useCerr = false;
        break;
    case LogLevel::ERR:
        levelStr = "ERROR";
        color = "\033[31m"; // Red color
        break;
    case LogLevel::FATAL:
        levelStr = "FATAL";
        color = "\033[31m"; // Red color
        break;
    }

    std::string timestamp = getCurTime();
    std::string logMessage = std::format("[{}][{}][{}]: {}", timestamp, levelStr, source, message);

    if (consoleOutput)
    {
        std::ostream &consoleStream = useCerr ? std::cerr : std::cout; // A stream reference that can pick cout/cerr once, then write through one common path.
        if (color[0] != '\0')
        {
            consoleStream << color; // Only emit ANSI color codes for WARN/ERR, so INFO logs stay plain and cheaper to print.
            consoleStream << logMessage;
            consoleStream << "\033[0m"; // Reset the console color after WARN/ERR so later output is not tinted.
        }
        else
        {
            consoleStream << logMessage;
        }

        consoleStream << '\n'; // '\n' is cheaper than std::endl because it avoids flushing the stream every single log call.
    }

    if (fileOutput)
    {
        logFile << logMessage << '\n'; // Write the log message to the file.
    }
}

void GameWIP::Logger::logDBWIN(LogLevel level, std::string_view source, std::string_view message)
{
    const char *levelStr = "UNKNOWN"; // String literals are cheaper than building std::string objects for fixed text like log level names.

    switch (level)
    {
    default: // Default case for unknown log levels.
        break;
    case LogLevel::INFO:
        levelStr = "INFO";
        break;
    case LogLevel::WARN:
        levelStr = "WARN";
        break;
    case LogLevel::ERR:
        levelStr = "ERROR";
        break;
    case LogLevel::FATAL:
        levelStr = "FATAL";
        break;
    }

    std::string timestamp = getCurTime();
    std::string logMessage = std::format("[{}][{}][{}]: {}\n", timestamp, levelStr, source, message);
    OutputDebugStringA(logMessage.c_str());
}

void GameWIP::Logger::fatalPopUp(std::string_view message)
{
    std::string messageText(message);
    MessageBoxA(nullptr, messageText.c_str(), "Fatal Error", MB_ICONERROR | MB_OK);
}