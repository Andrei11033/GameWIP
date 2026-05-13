#include "logger/logger.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <chrono>
#include <ctime>
#include <string>

namespace
{
    const char *getLevelText(GameWIP::LogLevel level)
    {
        switch (level)
        {
        case GameWIP::LogLevel::INFO:
            return "INFO";
        case GameWIP::LogLevel::WARN:
            return "WARN";
        case GameWIP::LogLevel::ERR:
            return "ERROR";
        case GameWIP::LogLevel::FATAL:
            return "FATAL";
        default:
            return "UNKNOWN";
        }
    }

    std::string getCurrentTimeText()
    {
        std::tm timeInfo{};
        auto now = std::chrono::system_clock::now();
        std::time_t time_s = std::chrono::system_clock::to_time_t(now);
        if (localtime_s(&timeInfo, &time_s) != 0)
        {
            return "invalid-time";
        }

        char timeBuffer[64]{};
        std::size_t written = std::strftime(timeBuffer, sizeof(timeBuffer), "%H:%M:%S", &timeInfo);
        return written == 0 ? "invalid-time" : std::string(timeBuffer, written);
    }

    std::string buildDebugOutputLine(GameWIP::LogLevel level, std::string_view source, std::string_view message)
    {
        std::string line;
        std::string timestamp = getCurrentTimeText();
        const char *levelText = getLevelText(level);

        line.reserve(timestamp.size() + source.size() + message.size() + 32);
        line.append("[");
        line.append(timestamp);
        line.append("][");
        line.append(levelText);
        line.append("][");
        line.append(source);
        line.append("]: ");
        line.append(message);
        line.push_back('\n');
        return line;
    }
}

void GameWIP::Logger::logDebugOutput(LogLevel level, std::string_view source, std::string_view message)
{
    std::string logMessage = buildDebugOutputLine(level, source, message);
    OutputDebugStringA(logMessage.c_str());
}

void GameWIP::Logger::showFatalPopup(std::string_view message)
{
    std::string messageText(message);
    MessageBoxA(nullptr, messageText.c_str(), "Fatal Error", MB_ICONERROR | MB_OK);
}
