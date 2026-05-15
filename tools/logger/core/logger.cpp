#include "logger/logger.h"
#include "logger/internal/logger_platform.h"

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdlib>
#include <ctime>
#include <exception>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <limits>
#include <mutex>
#include <queue>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

using GameWIP::LogLevel;
using GameWIP::OutputMode;
using LoggerResult = GameWIP::Logger::Result;

namespace
{
    constexpr std::size_t maxMessageLength = 4096; // Hard cap for one log message.

    struct LogEntry
    {
        LogLevel level = LogLevel::INFO;
        std::string source;
        std::string message;
    };

    struct LoggerState
    {
        LogLevel minLogLevel = LogLevel::INFO;
        OutputMode mode = OutputMode::BOTH;
        std::size_t maxQueueSize = 1024;
        std::size_t hardMaxQueueSize = 2048;

        std::ofstream logFile;
        std::string logFilePath;
        std::queue<LogEntry> logQueue;

        std::size_t droppedLogs = 0;
        bool dropWarningSent = false;
        bool hardDropWarningSent = false;

        std::mutex logMutex;
        std::condition_variable logCondition;
        std::thread loggingThread;
        bool workerRunning = false;
        bool workerBusy = false;
        bool shutdownRegistered = false;

        LoggerResult lastResult = LoggerResult::Success;
        unsigned long lastPlatformError = 0;

        bool hasCachedTimestamp = false;
        std::time_t cachedTimestampSecond = 0;
        std::string cachedTimestampText;
    };

    LoggerState loggerState;

    struct LogStyle
    {
        const char *text = "UNKNOWN";
        const char *color = "";
        bool useCerr = true;
    };

    LogStyle getLogStyle(LogLevel level)
    {
        switch (level)
        {
        case LogLevel::INFO:
            return {"INFO", "", false};
        case LogLevel::WARN:
            return {"WARN", "\033[33m", false};
        case LogLevel::ERR:
            return {"ERROR", "\033[31m", true};
        case LogLevel::FATAL:
            return {"FATAL", "\033[31m", true};
        }

        return {};
    }

    bool isLowPriority(LogLevel level)
    {
        return level == LogLevel::INFO || level == LogLevel::WARN;
    }

    std::size_t computeHardQueueLimit(std::size_t softLimit)
    {
        if (softLimit > std::numeric_limits<std::size_t>::max() / 2)
        {
            return std::numeric_limits<std::size_t>::max();
        }

        return softLimit * 2;
    }

    void recordResult(LoggerResult result, unsigned long platformError = 0)
    {
        std::lock_guard<std::mutex> lock(loggerState.logMutex);
        loggerState.lastResult = result;
        loggerState.lastPlatformError = platformError;
    }

    void clearQueue()
    {
        while (!loggerState.logQueue.empty())
        {
            loggerState.logQueue.pop();
        }
    }

    std::string formatTime(std::time_t time, const char *timeFormat)
    {
        std::tm timeInfo{};
        if (localtime_s(&timeInfo, &time) != 0)
        {
            return "invalid-time";
        }

        char timeBuffer[64]{};
        const std::size_t written = std::strftime(timeBuffer, sizeof(timeBuffer), timeFormat, &timeInfo);
        if (written == 0)
        {
            return "invalid-time";
        }

        return std::string(timeBuffer, written);
    }

    std::string getCurrentTimeText(const char *timeFormat)
    {
        const auto now = std::chrono::system_clock::now();
        const std::time_t time = std::chrono::system_clock::to_time_t(now);
        return formatTime(time, timeFormat);
    }

    std::string getCachedTimestampText()
    {
        const auto now = std::chrono::system_clock::now();
        const std::time_t currentSecond = std::chrono::system_clock::to_time_t(now);

        std::lock_guard<std::mutex> lock(loggerState.logMutex);
        if (!loggerState.hasCachedTimestamp || loggerState.cachedTimestampSecond != currentSecond)
        {
            loggerState.cachedTimestampSecond = currentSecond;
            loggerState.cachedTimestampText = formatTime(currentSecond, "%H:%M:%S");
            loggerState.hasCachedTimestamp = true;
        }

        return loggerState.cachedTimestampText;
    }

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

    void writeLogEntry(const LogEntry &entry)
    {
        if (entry.level < loggerState.minLogLevel || loggerState.mode == OutputMode::NONE)
        {
            return;
        }

        const bool consoleOutput = loggerState.mode == OutputMode::CONSOLE || loggerState.mode == OutputMode::BOTH;
        const bool fileOutput = (loggerState.mode == OutputMode::FILE || loggerState.mode == OutputMode::BOTH) && loggerState.logFile.is_open();
        if (!consoleOutput && !fileOutput)
        {
            return;
        }

        const LogStyle style = getLogStyle(entry.level);
        const std::string timestamp = getCachedTimestampText();
        const std::string logMessage = buildLogMessage(timestamp, style.text, entry.source, entry.message);

        if (consoleOutput)
        {
            std::ostream &consoleStream = style.useCerr ? std::cerr : std::cout;
            if (style.color[0] != '\0')
            {
                consoleStream << style.color << logMessage << "\033[0m";
            }
            else
            {
                consoleStream << logMessage;
            }

            consoleStream << '\n';
        }

        if (fileOutput)
        {
            loggerState.logFile << logMessage << '\n';
        }
    }

    void loggerWorker()
    {
        while (true)
        {
            std::queue<LogEntry> localQueue;

            {
                std::unique_lock<std::mutex> lock(loggerState.logMutex);
                loggerState.logCondition.wait(lock, []
                                              { return !loggerState.logQueue.empty() || !loggerState.workerRunning; });

                if (loggerState.logQueue.empty() && !loggerState.workerRunning)
                {
                    break;
                }

                loggerState.logQueue.swap(localQueue);
                loggerState.workerBusy = true;
            }

            while (!localQueue.empty())
            {
                writeLogEntry(localQueue.front());
                localQueue.pop();
            }

            {
                std::unique_lock<std::mutex> lock(loggerState.logMutex);
                if (loggerState.dropWarningSent && loggerState.logQueue.size() <= loggerState.maxQueueSize / 2)
                {
                    loggerState.dropWarningSent = false;
                }

                if (loggerState.hardDropWarningSent && loggerState.logQueue.size() <= loggerState.hardMaxQueueSize / 2)
                {
                    loggerState.hardDropWarningSent = false;
                }

                loggerState.workerBusy = false;
            }

            loggerState.logCondition.notify_all();
        }
    }

    void shutdownLoggerAtExit()
    {
        bool workerActive = false;
        {
            std::lock_guard<std::mutex> lock(loggerState.logMutex);
            workerActive = loggerState.workerRunning;
        }

        if (workerActive)
        {
            GameWIP::Logger::shutdown();
        }
    }
}

LogLevel GameWIP::Logger::getMinLogLevel()
{
    std::lock_guard<std::mutex> lock(loggerState.logMutex);
    return loggerState.minLogLevel;
}

OutputMode GameWIP::Logger::getOutputMode()
{
    std::lock_guard<std::mutex> lock(loggerState.logMutex);
    return loggerState.mode;
}

std::size_t GameWIP::Logger::getDroppedLogCount()
{
    std::lock_guard<std::mutex> lock(loggerState.logMutex);
    return loggerState.droppedLogs;
}

GameWIP::Logger::Result GameWIP::Logger::getLastResult()
{
    std::lock_guard<std::mutex> lock(loggerState.logMutex);
    return loggerState.lastResult;
}

unsigned long GameWIP::Logger::getLastPlatformError()
{
    std::lock_guard<std::mutex> lock(loggerState.logMutex);
    return loggerState.lastPlatformError;
}

GameWIP::Logger::Result GameWIP::Logger::init(OutputMode newMode, LogLevel newLevel, std::size_t newMaxQueueSize)
{
    bool workerActive = false;
    {
        std::lock_guard<std::mutex> lock(loggerState.logMutex);
        workerActive = loggerState.workerRunning;
        if (workerActive)
        {
            loggerState.lastResult = Result::AlreadyRunning;
            loggerState.lastPlatformError = 0;
        }
    }

    if (workerActive)
    {
        constexpr std::string_view alreadyRunningMessage = "Logger::init() called while the logger is already running. Ignoring new configuration.";
        log(LogLevel::ERR, "Logger-Init", alreadyRunningMessage);
        logDebugOutput(LogLevel::ERR, "Logger-Init", alreadyRunningMessage);
        return Result::AlreadyRunning;
    }

    if (!loggerState.shutdownRegistered)
    {
        std::atexit(shutdownLoggerAtExit);
        loggerState.shutdownRegistered = true;
    }

    LoggerResult initResult = Result::Success;
    if (newMaxQueueSize == 0)
    {
        newMaxQueueSize = 1;
        initResult = Result::InvalidQueueSize;
    }

    if (loggerState.logFile.is_open())
    {
        loggerState.logFile.close();
    }

    loggerState.logFilePath.clear();
    loggerState.mode = newMode;
    loggerState.minLogLevel = newLevel;
    loggerState.maxQueueSize = newMaxQueueSize;
    loggerState.hardMaxQueueSize = computeHardQueueLimit(loggerState.maxQueueSize);
    loggerState.droppedLogs = 0;
    loggerState.dropWarningSent = false;
    loggerState.hardDropWarningSent = false;
    loggerState.workerBusy = false;
    loggerState.hasCachedTimestamp = false;
    loggerState.cachedTimestampSecond = 0;
    loggerState.cachedTimestampText.clear();
    clearQueue();

    if (loggerState.mode == OutputMode::NONE)
    {
        recordResult(initResult);
        return initResult;
    }

    const bool wantsFile = loggerState.mode == OutputMode::FILE || loggerState.mode == OutputMode::BOTH;
    bool fileSetupFailed = false;
    std::string fileErrorMessage;

    if (wantsFile)
    {
        try
        {
            std::filesystem::create_directory("logs");
            loggerState.logFilePath = "logs/" + getCurrentTimeText("%Y-%m-%d_%H-%M-%S") + ".log";
            loggerState.logFile.open(loggerState.logFilePath);

            if (!loggerState.logFile.is_open())
            {
                loggerState.mode = OutputMode::CONSOLE;
                initResult = Result::FileOpenFailed;
                fileSetupFailed = true;
                fileErrorMessage = std::format("Failed to open log file at: {}. Falling back to console output.", loggerState.logFilePath);
            }
        }
        catch (const std::exception &error)
        {
            loggerState.mode = OutputMode::CONSOLE;
            initResult = Result::FileSetupFailed;
            fileSetupFailed = true;

            if (loggerState.logFilePath.empty())
            {
                fileErrorMessage = std::format("Logger file setup failed: {}. Falling back to console output.", error.what());
            }
            else
            {
                fileErrorMessage = std::format("Logger file setup failed for {}: {}. Falling back to console output.", loggerState.logFilePath, error.what());
            }
        }
        catch (...)
        {
            loggerState.mode = OutputMode::CONSOLE;
            initResult = Result::FileSetupFailed;
            fileSetupFailed = true;

            if (loggerState.logFilePath.empty())
            {
                fileErrorMessage = "Logger file setup failed with an unknown error. Falling back to console output.";
            }
            else
            {
                fileErrorMessage = std::format("Logger file setup failed for {} with an unknown error. Falling back to console output.", loggerState.logFilePath);
            }
        }
    }

    loggerState.workerRunning = true;
    loggerState.loggingThread = std::thread(loggerWorker);
    recordResult(initResult);

    if (fileSetupFailed)
    {
        log(LogLevel::ERR, "Logger-Init", fileErrorMessage);
        logDebugOutput(LogLevel::ERR, "Logger-Init", fileErrorMessage);
    }

    return initResult;
}

void GameWIP::Logger::flush()
{
    {
        std::unique_lock<std::mutex> lock(loggerState.logMutex);
        loggerState.logCondition.wait(lock, []
                                      { return loggerState.logQueue.empty() && !loggerState.workerBusy; });
    }

    std::cout.flush();
    std::cerr.flush();

    if (loggerState.logFile.is_open())
    {
        loggerState.logFile.flush();
    }
}

void GameWIP::Logger::shutdown()
{
    std::size_t droppedCount = 0;
    {
        std::unique_lock<std::mutex> lock(loggerState.logMutex);
        loggerState.workerRunning = false;
        droppedCount = loggerState.droppedLogs;
    }

    loggerState.logCondition.notify_all();

    if (loggerState.loggingThread.joinable())
    {
        loggerState.loggingThread.join();
    }

    if (droppedCount > 0)
    {
        const std::string droppedMessage = std::format("Logger had {} dropped log messages.", droppedCount);
        writeLogEntry(LogEntry{LogLevel::WARN, "Logger-Shutdown", droppedMessage});
        logDebugOutput(LogLevel::WARN, "Logger-Shutdown", droppedMessage);
    }

    flush();

    if (loggerState.logFile.is_open())
    {
        loggerState.logFile.close();
    }

    loggerState.logFilePath.clear();
    loggerState.mode = OutputMode::NONE;
    loggerState.workerBusy = false;
    recordResult(Result::Success);
}

void GameWIP::Logger::log(LogLevel entryLevel, std::string_view source, std::string_view message)
{
    bool shouldWarnSoftLimit = false;
    bool shouldWarnHardLimit = false;
    bool didEnqueue = false;
    std::size_t warningQueueSize = 0;
    {
        std::unique_lock<std::mutex> lock(loggerState.logMutex);
        if (!loggerState.workerRunning || loggerState.mode == OutputMode::NONE || entryLevel < loggerState.minLogLevel)
        {
            return;
        }

        const std::size_t queueSize = loggerState.logQueue.size();
        if (queueSize >= loggerState.hardMaxQueueSize)
        {
            ++loggerState.droppedLogs;
            if (!loggerState.hardDropWarningSent)
            {
                loggerState.hardDropWarningSent = true;
                shouldWarnHardLimit = true;
                warningQueueSize = queueSize;
            }
        }
        else if (queueSize >= loggerState.maxQueueSize && isLowPriority(entryLevel))
        {
            ++loggerState.droppedLogs;
            if (!loggerState.dropWarningSent)
            {
                loggerState.dropWarningSent = true;
                shouldWarnSoftLimit = true;
                warningQueueSize = queueSize;
            }
        }
        else
        {
            loggerState.logQueue.push(LogEntry{entryLevel, std::string(source), truncateMessage(message)});
            didEnqueue = true;
        }
    }

    if (shouldWarnHardLimit)
    {
        logDebugOutput(LogLevel::WARN, "Logger", std::format("Log queue hit the hard limit ({} entries). Dropping all severities until it drains.", warningQueueSize));
    }
    else if (shouldWarnSoftLimit)
    {
        logDebugOutput(LogLevel::WARN, "Logger", std::format("Log queue is full ({} entries). Dropping INFO/WARN until it drains.", warningQueueSize));
    }

    if (didEnqueue)
    {
        loggerState.logCondition.notify_one();
    }
}

void GameWIP::Logger::logDebugOutput(LogLevel level, std::string_view source, std::string_view message)
{
    const LogStyle style = getLogStyle(level);
    std::string line = buildLogMessage(getCachedTimestampText(), style.text, source, message);
    line.push_back('\n');
    GameWIP::LoggerPlatform::writeDebugOutput(line);
}

void GameWIP::Logger::showFatalPopup(std::string_view message)
{
    if (!GameWIP::LoggerPlatform::showFatalPopup(message))
    {
        recordResult(Result::PlatformCallFailed);
    }
}
