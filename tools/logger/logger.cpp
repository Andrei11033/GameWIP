#include "logger.h" // Include the corresponding header file for the Logger class.

#include <chrono>             // For getting the current time (std::chrono::system_clock).
#include <condition_variable> // For std::condition_variable to signal the logging thread when new log entries are added.
#include <cstddef>            // For std::size_t.
#include <cstdlib>            // For std::atexit.
#include <ctime>              // For converting time to a struct (std::localtime).
#include <exception>          // For std::exception when filesystem setup throws.
#include <filesystem>         // For creating the logs directory (std::filesystem::create_directory).
#include <format>             // For formatting the final log message (std::format).
#include <fstream>            // For file output (std::ofstream).
#include <iostream>           // For console output (std::cout and std::cerr).
#include <limits>             // For std::numeric_limits.
#include <mutex>              // For std::mutex to protect shared resources in a multithreaded environment.
#include <queue>              // For std::queue.
#include <string>             // For std::string.
#include <string_view>        // For std::string_view.
#include <thread>             // For std::thread to run the logging thread.
#include <utility>            // For std::move, for optimizing string handling.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h> // For Windows-specific functions like OutputDebugString and MessageBox.

using GameWIP::LogLevel;
using GameWIP::OutputMode;

// Anonymous namespace, used to contain internal vars.
namespace
{
    // Internal logger variables.
    LogLevel minLogLevel = LogLevel::INFO;         // The minimum log level to output.
    OutputMode mode = OutputMode::BOTH;            // The output mode, determines where the logs will be outputted (console, file, both, or none).
    std::size_t maxQueueSize = 1024;               // The soft queue limit, where low-priority logs begin to drop.
    std::size_t hardMaxQueueSize = 2048;           // The hard queue limit, where all logs begin to drop.
    constexpr std::size_t maxMessageLength = 4096; // Hard cap for one log message, to stop one log call from growing RAM too much.
    bool shutdownRegistered = false;               // Ensures atexit cleanup is only registered once.

    std::ofstream logFile;   // The active log file stream used when file output is enabled.
    std::string logFilePath; // The full path to the current log file.
    struct LogEntry          // A struct to represent a log entry.
    {
        LogLevel level;
        std::string source;
        std::string message;
    };
    std::queue<LogEntry> logQueue; // A queue to hold log entries.

    std::size_t droppedLogs = 0;      // A counter for how many log entries have been dropped.
    bool dropWarningSent = false;     // A flag to track if a warning about dropped logs has been sent.
    bool hardDropWarningSent = false; // A flag to track if a warning about the hard queue limit has been sent.

    std::mutex logMutex;                  // A mutex to protect access to the log queue.
    std::condition_variable logCondition; // A condition variable to signal the logging thread when new log entries are added.
    std::thread loggingThread;            // The thread that will process log entries from the queue.
    bool workerRunning = false;           // A flag to control the logging thread's main loop.
    bool workerBusy = false;              // A flag that tells if the worker is actively writing.

    struct LogStyle // A struct to hold the display style for each log level.
    {
        const char *text;
        const char *color;
        bool useCerr;
    };

    /// @brief Gets the display style for a given log level.
    /// @param level The log level.
    /// @return The display style for the log level.
    LogStyle getLogStyle(LogLevel level)
    {
        switch (level)
        {
        default:
            return {"UNKNOWN", "", true};
        case LogLevel::INFO:
            return {"INFO", "", false};
        case LogLevel::WARN:
            return {"WARN", "\033[33m", false};
        case LogLevel::ERR:
            return {"ERROR", "\033[31m", true};
        case LogLevel::FATAL:
            return {"FATAL", "\033[31m", true};
        }
    }

    /// @brief Determines if a log level is considered low priority (INFO or WARN).
    /// @param level The log level.
    /// @return True if the log level is low priority, false otherwise.
    bool isLowPriority(LogLevel level)
    {
        return level == LogLevel::INFO || level == LogLevel::WARN;
    }

    /// @brief Computes the hard queue limit based on the soft queue limit.
    /// @param softLimit The soft queue limit.
    /// @return The computed hard queue limit.
    std::size_t computeHardQueueLimit(std::size_t softLimit)
    {
        // To prevent overflow, if doubling the soft limit would exceed the maximum value for std::size_t, we clamp it to the maximum.
        if (softLimit > std::numeric_limits<std::size_t>::max() / 2)
        {
            return std::numeric_limits<std::size_t>::max();
        }

        return softLimit * 2;
    }

    /// @brief A function registered with atexit to ensure the logger is properly shut down when the program exits.
    /// @return None.
    void shutdownLoggerAtExit()
    {
        bool workerActive;
        {
            std::lock_guard<std::mutex> lock(logMutex);
            workerActive = workerRunning;
        }

        if (workerActive)
        {
            GameWIP::Logger::shutdown();
        }
    }

    /// @brief Truncates a log message to the maximum allowed length.
    /// @param message The log message to truncate.
    /// @return The truncated log message.
    std::string truncateMessage(std::string_view message)
    {
        if (message.size() <= maxMessageLength)
        {
            return std::string(message);
        }

        constexpr std::string_view suffix = "... [truncated]";
        constexpr std::size_t suffixLength = suffix.size();

        if (maxMessageLength <= suffixLength)
        {
            return std::string(suffix.substr(0, maxMessageLength));
        }

        std::string truncatedMessage;
        truncatedMessage.reserve(maxMessageLength);
        truncatedMessage.append(message.data(), maxMessageLength - suffixLength);
        truncatedMessage.append(suffix);
        return truncatedMessage;
    }

    /// @brief Gets the current time as a string.
    /// @param timeFormat The format string for the time.
    /// @return The current time as a string.
    std::string getCurTime(const char *timeFormat = "%H:%M:%S")
    {
        std::tm timeInfo{};                                             // A struct that holds the time components.
        auto now = std::chrono::system_clock::now();                    // get the current time, from "Unix epoch".
        std::time_t time_s = std::chrono::system_clock::to_time_t(now); // convert to time_t, round to nearest second.
        auto result = localtime_s(&timeInfo, &time_s);                  // Convert time_s to local time.
        if (result != 0)
        {
            return "invalid-time"; // If localtime_s fails, return a placeholder string.
        }

        char timeBuffer[64]{};
        std::size_t written = std::strftime(timeBuffer, sizeof(timeBuffer), timeFormat, &timeInfo);
        if (written == 0)
        {
            return "invalid-time";
        }

        return std::string(timeBuffer, written);
    }

    /// @brief Builds a complete log line from already prepared fields.
    /// @param timestamp The timestamp string for the log entry.
    /// @param levelText The display text for the log level.
    /// @param source The source tag for the log entry.
    /// @param message The log message.
    /// @return The complete log line.
    std::string buildLogMessage(std::string_view timestamp, std::string_view levelText, std::string_view source, std::string_view message)
    {
        constexpr std::size_t fixedFormatLength = 8; // "[", "][", "][", and "]: ".

        std::string logMessage;
        logMessage.reserve(timestamp.size() + levelText.size() + source.size() + message.size() + fixedFormatLength);
        logMessage.append("[");
        logMessage.append(timestamp);
        logMessage.append("][");
        logMessage.append(levelText);
        logMessage.append("][");
        logMessage.append(source);
        logMessage.append("]: ");
        logMessage.append(message);
        return logMessage;
    }

    /// @brief Writes a log entry to the appropriate output streams.
    /// @param entry The log entry to write.
    void writeLogEntry(const LogEntry &entry)
    {
        if (entry.level < minLogLevel || mode == OutputMode::NONE)
        {
            return;
        }

        bool consoleOutput = (mode == OutputMode::CONSOLE || mode == OutputMode::BOTH);                  // Determine if console output is enabled based on the current output mode.
        bool fileOutput = ((mode == OutputMode::FILE || mode == OutputMode::BOTH) && logFile.is_open()); // Determine if file output is enabled based on the current output mode.
        if (!consoleOutput && !fileOutput)
        {
            return;
        }

        LogStyle style = getLogStyle(entry.level);
        std::string timestamp = getCurTime();
        std::string logMessage = buildLogMessage(timestamp, style.text, entry.source, entry.message);

        if (consoleOutput)
        {
            std::ostream &consoleStream = style.useCerr ? std::cerr : std::cout; // A stream reference that can pick cout/cerr once, then write through one common path.
            if (style.color[0] != '\0')
            {
                consoleStream << style.color;
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

    /// @brief The main worker function for the logging thread.
    void loggerWorker()
    {
        while (true)
        {
            std::queue<LogEntry> localQueue;

            {
                std::unique_lock<std::mutex> lock(logMutex);
                logCondition.wait(lock, []
                                  { return !logQueue.empty() || !workerRunning; });

                if (logQueue.empty() && !workerRunning)
                {
                    break;
                }

                logQueue.swap(localQueue);
                workerBusy = true;
            }

            while (!localQueue.empty())
            {
                writeLogEntry(localQueue.front());
                localQueue.pop();
            }

            {
                std::unique_lock<std::mutex> lock(logMutex);
                if (dropWarningSent && logQueue.size() <= maxQueueSize / 2)
                {
                    dropWarningSent = false;
                }

                if (hardDropWarningSent && logQueue.size() <= hardMaxQueueSize / 2)
                {
                    hardDropWarningSent = false;
                }

                workerBusy = false;
            }

            logCondition.notify_all();
        }
    }
}

LogLevel GameWIP::Logger::getMinLogLevel()
{
    std::lock_guard<std::mutex> lock(logMutex);
    return minLogLevel;
}

OutputMode GameWIP::Logger::getOutputMode()
{
    std::lock_guard<std::mutex> lock(logMutex);
    return mode;
}

std::size_t GameWIP::Logger::getDroppedLogCount()
{
    std::lock_guard<std::mutex> lock(logMutex);
    return droppedLogs;
}

void GameWIP::Logger::init(OutputMode newMode, LogLevel newLevel, std::size_t newMaxQueueSize)
{
    bool workerActive;
    {
        std::lock_guard<std::mutex> lock(logMutex);
        workerActive = workerRunning;
    }

    if (workerActive)
    {
        constexpr std::string_view alreadyRunningMessage = "Logger::init() called while the logger is already running. Ignoring new configuration.";
        log(LogLevel::ERR, "Logger-Init", alreadyRunningMessage);
        logDBWIN(LogLevel::ERR, "Logger-Init", alreadyRunningMessage);
        return;
    }

    if (!shutdownRegistered)
    {
        std::atexit(shutdownLoggerAtExit);
        shutdownRegistered = true;
    }

    if (logFile.is_open()) // If a log file is already open, close it before creating a new one.
    {
        logFile.close();
    }

    if (newMaxQueueSize == 0)
    {
        newMaxQueueSize = 1; // Clamp invalid values to the smallest usable queue size.
    }

    logFilePath.clear();            // Clear the old file path when re-initializing.
    mode = newMode;                 // Store the new output mode.
    minLogLevel = newLevel;         // Store the new minimum log level.
    maxQueueSize = newMaxQueueSize; // Store the maximum queue size.
    hardMaxQueueSize = computeHardQueueLimit(maxQueueSize);
    droppedLogs = 0;             // Reset the dropped-log counter for a fresh logger run.
    dropWarningSent = false;     // Reset the drop warning flag for a fresh logger run.
    hardDropWarningSent = false; // Reset the hard drop warning flag for a fresh logger run.
    workerBusy = false;          // Reset the worker state for a fresh logger run.

    while (!logQueue.empty())
    {
        logQueue.pop();
    }

    if (mode == OutputMode::NONE)
    {
        return;
    }

    bool wantsFile = mode == OutputMode::FILE || mode == OutputMode::BOTH;
    bool fileOpenFailed = false;
    std::string fileErrorMessage;

    if (wantsFile)
    {
        try
        {
            std::filesystem::create_directory("logs"); // Create the logs directory if it doesn't exist.

            std::string curTime = getCurTime("%Y-%m-%d_%H-%M-%S"); // Get the current time in a format suitable for filenames.
            logFilePath = "logs/" + curTime + ".log";              // Create the full path for the new log file.

            logFile.open(logFilePath); // Open the log file for writing.

            if (!logFile.is_open())
            {
                mode = OutputMode::CONSOLE;
                fileOpenFailed = true;
                fileErrorMessage = std::format("Failed to open log file at: {}. Falling back to console output.", logFilePath);
            }
        }
        catch (const std::exception &error)
        {
            mode = OutputMode::CONSOLE;
            fileOpenFailed = true;

            if (logFilePath.empty())
            {
                fileErrorMessage = std::format("Logger file setup failed: {}. Falling back to console output.", error.what());
            }
            else
            {
                fileErrorMessage = std::format("Logger file setup failed for {}: {}. Falling back to console output.", logFilePath, error.what());
            }
        }
        catch (...)
        {
            mode = OutputMode::CONSOLE;
            fileOpenFailed = true;

            if (logFilePath.empty())
            {
                fileErrorMessage = "Logger file setup failed with an unknown error. Falling back to console output.";
            }
            else
            {
                fileErrorMessage = std::format("Logger file setup failed for {} with an unknown error. Falling back to console output.", logFilePath);
            }
        }
    }

    workerRunning = true;
    loggingThread = std::thread(loggerWorker);

    if (fileOpenFailed)
    {
        log(LogLevel::ERR, "Logger-Init", fileErrorMessage);
        logDBWIN(LogLevel::ERR, "Logger-Init", fileErrorMessage);
    }
}

void GameWIP::Logger::flush()
{
    {
        std::unique_lock<std::mutex> lock(logMutex);
        logCondition.wait(lock, []
                          { return logQueue.empty() && !workerBusy; });
    }

    std::cout.flush();
    std::cerr.flush();

    if (logFile.is_open())
    {
        logFile.flush();
    }
}

void GameWIP::Logger::shutdown()
{
    std::size_t droppedCount;
    {
        std::unique_lock<std::mutex> lock(logMutex);
        workerRunning = false;
        droppedCount = droppedLogs;
    }

    logCondition.notify_all();

    if (loggingThread.joinable())
    {
        loggingThread.join();
    }

    if (droppedCount > 0)
    {
        writeLogEntry(LogEntry{LogLevel::WARN, "Logger-Shutdown", std::format("Logger had {} dropped log messages.", droppedCount)});
        logDBWIN(LogLevel::WARN, "Logger-Shutdown", std::format("Logger had {} dropped log messages.", droppedCount));
    }

    flush();

    if (logFile.is_open())
    {
        logFile.close();
    }

    logFilePath.clear();
    mode = OutputMode::NONE;
    workerBusy = false;
}

void GameWIP::Logger::log(LogLevel entryLevel, std::string_view source, std::string_view message)
{
    bool shouldWarnSoftLimit = false;
    bool shouldWarnHardLimit = false;
    bool didEnqueue = false;
    std::size_t warningQueueSize = 0;
    {
        std::unique_lock<std::mutex> lock(logMutex);
        if (!workerRunning || mode == OutputMode::NONE || entryLevel < minLogLevel)
        {
            return;
        }

        std::size_t queueSize = logQueue.size();
        if (queueSize >= hardMaxQueueSize)
        {
            droppedLogs++;
            if (!hardDropWarningSent)
            {
                hardDropWarningSent = true;
                shouldWarnHardLimit = true;
                warningQueueSize = queueSize;
            }
        }
        else if (queueSize >= maxQueueSize && isLowPriority(entryLevel))
        {
            droppedLogs++;
            if (!dropWarningSent)
            {
                dropWarningSent = true;
                shouldWarnSoftLimit = true;
                warningQueueSize = queueSize;
            }
        }
        else
        {
            LogEntry entry;

            entry.level = entryLevel;
            entry.source = source;
            entry.message = truncateMessage(message);

            logQueue.push(std::move(entry));
            didEnqueue = true;
        }
    }

    if (shouldWarnHardLimit)
    {
        logDBWIN(LogLevel::WARN, "Logger", std::format("Log queue hit the hard limit ({} entries). Dropping all severities until it drains.", warningQueueSize));
    }
    else if (shouldWarnSoftLimit)
    {
        logDBWIN(LogLevel::WARN, "Logger", std::format("Log queue is full ({} entries). Dropping INFO/WARN until it drains.", warningQueueSize));
    }

    if (didEnqueue)
    {
        logCondition.notify_one();
    }
}

void GameWIP::Logger::logDBWIN(LogLevel level, std::string_view source, std::string_view message)
{
    LogStyle style = getLogStyle(level);
    std::string timestamp = getCurTime();
    std::string logMessage = buildLogMessage(timestamp, style.text, source, message);
    logMessage.push_back('\n');
    OutputDebugStringA(logMessage.c_str());
}

void GameWIP::Logger::fatalPopUp(std::string_view message)
{
    std::string messageText(message);
    MessageBoxA(nullptr, messageText.c_str(), "Fatal Error", MB_ICONERROR | MB_OK);
}