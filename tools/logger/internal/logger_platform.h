#pragma once

#include "logger/logger.h"

#include <string_view>

namespace GameWIP::LoggerPlatform
{
    /// @brief Writes a fully formatted log line to the platform debug output.
    /// @param line Log line, including any desired trailing newline.
    void writeDebugOutput(std::string_view line);

    /// @brief Displays a fatal error popup through the platform UI.
    /// @param message Message to display.
    /// @return True when the platform reports that the popup was shown.
    bool showFatalPopup(std::string_view message);
}
