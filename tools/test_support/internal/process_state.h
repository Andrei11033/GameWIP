/// @file process_state.h
/// @brief Private process-state coordination access for TestSupport.

#pragma once

#include <mutex>

namespace GameWIP::TestSupport::Internal
{
    /// ------------------------------------------------------------
    /// Process-state coordination
    /// ------------------------------------------------------------

    [[nodiscard]] std::recursive_mutex &processStateMutex() noexcept;
} // namespace GameWIP::TestSupport::Internal
