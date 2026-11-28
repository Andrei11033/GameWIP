/// @file process.cpp
/// @brief Logger process metadata bridge.

#include "logger/detail/formatting.h"
#include "logger/platform/backend.h"

namespace GameWIP::Logger::Internal::Core
{
    /// ------------------------------------------------------------
    /// Process metadata
    /// ------------------------------------------------------------

    std::uint32_t currentProcessId() noexcept
    {
        return GameWIP::Logger::Platform::currentProcessId();
    }
} // namespace GameWIP::Logger::Internal::Core
