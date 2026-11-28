/// @file helpers.cpp
/// @brief Shared Win32 validation helpers.

#include "validation/platform_tests.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdlib>
#include <string>

namespace GameWIP::Validation::PlatformTests
{
    /// ------------------------------------------------------------
    /// Process and environment helpers
    /// ------------------------------------------------------------

    DebuggerStatus debuggerStatus() noexcept
    {
        return IsDebuggerPresent() != FALSE ? DebuggerStatus::Attached : DebuggerStatus::Detached;
    }

    void suppressCrashDialogs() noexcept
    {
        SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    }

    bool setEnvironmentVariable(std::string_view name, const char *value) noexcept
    {
        try
        {
            return _putenv_s(std::string(name).c_str(), value == nullptr ? "" : value) == 0;
        }
        catch(...)
        {
            return false;
        }
    }
} // namespace GameWIP::Validation::PlatformTests
