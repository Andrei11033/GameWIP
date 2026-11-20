/// @file process.cpp
/// @brief Logger process metadata bridge.

#include "logger/internal/core.h"

namespace GameWIP::Logger::Detail::Core
{
    /// ------------------------------------------------------------
    /// Process metadata
    /// ------------------------------------------------------------

    std::uint32_t currentProcessId() noexcept
    {
        return GameWIP::Logger::Detail::Platform::currentProcessId();
    }
} // namespace GameWIP::Logger::Detail::Core
