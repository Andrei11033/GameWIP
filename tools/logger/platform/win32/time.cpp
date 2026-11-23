/// @file time.cpp
/// @brief Win32 local-time formatting.

#include "logger/platform/backend.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <ctime>
#include <new>
#include <ranges>
#include <string>

namespace GameWIP::Logger::Platform
{
    IO::Types::Status formatLocalTime(std::time_t time, std::string_view timeFormat, std::string &outText)
    {
        outText.clear();

        // Convert the timestamp and bound the runtime format before rendering.
        std::tm timeInfo{};
        const int localTimeResult = localtime_s(&timeInfo, &time);

        if(localTimeResult != 0)
        {
            return IO::makeStatus(IO::Types::ErrorCode::NativeFailure, localTimeResult);
        }

        if(timeFormat.size() >= 64)
        {
            return IO::makeStatus(IO::Types::ErrorCode::InvalidArgument, ERROR_INVALID_DATA);
        }

        char formatBuffer[64]{};

        if(!timeFormat.empty())
        {
            std::ranges::copy(timeFormat, formatBuffer);
        }

        char timeBuffer[128]{};
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-nonliteral"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
#endif
        // Runtime strftime formats are intentional here. The format is length-bounded,
        // null-terminated, and has no variadic arguments whose types could mismatch it.
        const std::size_t written = std::strftime(timeBuffer, sizeof(timeBuffer), formatBuffer, &timeInfo);
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
        if(written == 0)
        {
            return IO::makeStatus(IO::Types::ErrorCode::NativeFailure, ERROR_INVALID_DATA);
        }

        try
        {
            // Retain the formatted text only after native conversion succeeds.
            outText.assign(timeBuffer, written);
            return {};
        }
        catch(const std::bad_alloc &)
        {
            outText.clear();
            return IO::makeStatus(IO::Types::ErrorCode::OutOfMemory);
        }
        catch(...)
        {
            outText.clear();
            return IO::makeStatus(IO::Types::ErrorCode::Unknown);
        }
    }
} // namespace GameWIP::Logger::Platform
