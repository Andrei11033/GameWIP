/// @file process.cpp
/// @brief Win32 process identifiers and memory counters.

#include "logger/platform/backend.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>

namespace GameWIP::Logger::Detail::Platform
{
    ProcessMemory queryProcessMemory()
    {
        PROCESS_MEMORY_COUNTERS_EX counters{};
        counters.cb = sizeof(counters);

        if(GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&counters), sizeof(counters)) == 0)
        {
            return {};
        }
        return {static_cast<std::size_t>(counters.WorkingSetSize), static_cast<std::size_t>(counters.PrivateUsage), true};
    }

    std::uint32_t currentProcessId() noexcept
    {
        return static_cast<std::uint32_t>(GetCurrentProcessId());
    }
} // namespace GameWIP::Logger::Detail::Platform
