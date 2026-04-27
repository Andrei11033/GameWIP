#include "assert.h"
#include "logger/logger.h"

#include <windows.h>
#include <string>

namespace GameWIP::Debug
{
    void handleAssertFailure(std::string_view conditionText, std::string_view message, std::string_view file, int line, std::string_view function)
    {
        std::string assertMessage;
        assertMessage.reserve(conditionText.size() + message.size() + file.size() + function.size() + 64); // Reserve extra space for formatting and line number.
        assertMessage.append(conditionText);
        if (!message.empty())
        {
            assertMessage.append("\nMessage: ");
            assertMessage.append(message);
        }
        assertMessage.append("\nLocation: ");
        assertMessage.append(file);
        assertMessage.append(":");
        assertMessage.append(std::to_string(line));
        assertMessage.append(" (");
        assertMessage.append(function);
        assertMessage.append(")");

        Logger::log(LogLevel::ERR, "Assert", assertMessage);
        Logger::logDBWIN(LogLevel::ERR, "Assert", assertMessage);

        Logger::flush();

        DebugBreak();
    }
}