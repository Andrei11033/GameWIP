/// @file executable_path.cpp
/// @brief Win32 executable discovery with a growing UTF-16 path buffer.

#include "validation/executable_path.h"

#include <windows.h>

#include <limits>
#include <string>
#include <system_error>

namespace GameWIP::Validation
{
    std::filesystem::path currentExecutablePath()
    {
        std::wstring buffer(256, L'\0');

        for (;;)
        {
            const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));

            if (length == 0)
            {
                throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), "GetModuleFileNameW");
            }

            if (length < buffer.size())
            {
                buffer.resize(length);
                return std::filesystem::path(buffer);
            }

            if (buffer.size() > std::numeric_limits<DWORD>::max() / 2)
            {
                throw std::system_error(std::make_error_code(std::errc::filename_too_long), "GetModuleFileNameW");
            }

            buffer.resize(buffer.size() * 2);
        }
    }
} // namespace GameWIP::Validation
