#include "debug/assert/assert.h"
#include "debug/assert/internal/assert_platform.h"
#include "logger/logger.h"

#include <string>

namespace
{
    std::string buildFailureMessage(std::string_view conditionText, std::string_view message, std::string_view file, int line, std::string_view function)
    {
        std::string failureMessage;
        failureMessage.reserve(conditionText.size() + message.size() + file.size() + function.size() + 64);
        failureMessage.append(conditionText);
        if (!message.empty())
        {
            failureMessage.append("\nMessage: ");
            failureMessage.append(message);
        }
        failureMessage.append("\nLocation: ");
        failureMessage.append(file);
        failureMessage.append(":");
        failureMessage.append(std::to_string(line));
        failureMessage.append(" (");
        failureMessage.append(function);
        failureMessage.append(")");
        return failureMessage;
    }

    void reportFailure(std::string_view source, std::string_view conditionText, std::string_view message, std::string_view file, int line, std::string_view function)
    {
        const std::string failureMessage = buildFailureMessage(conditionText, message, file, line, function);
        GameWIP::Logger::reportError(source, failureMessage);
    }
}

namespace GameWIP::Debug
{
    void handleAssertFailure(std::string_view conditionText, std::string_view message, std::string_view file, int line, std::string_view function)
    {
        reportFailure("Assert", conditionText, message, file, line, function);
        Platform::debugBreak();
    }

    void handleCheckFailure(std::string_view conditionText, std::string_view message, std::string_view file, int line, std::string_view function)
    {
        reportFailure("Check", conditionText, message, file, line, function);
    }
}
