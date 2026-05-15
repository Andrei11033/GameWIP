#include "logger/internal/logger_platform.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <string>

namespace GameWIP::LoggerPlatform
{
    void writeDebugOutput(std::string_view line)
    {
        std::string output(line);
        OutputDebugStringA(output.c_str());
    }

    bool showFatalPopup(std::string_view message)
    {
        std::string messageText(message);
        return MessageBoxA(nullptr, messageText.c_str(), "Fatal Error", MB_ICONERROR | MB_OK) != 0;
    }
}
