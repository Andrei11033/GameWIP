/// @file types.h
/// @brief Shared passive vocabulary for the TestSupport library.

#pragma once

#include "status/result.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace GameWIP::TestSupport
{
    /// @brief Passive TestSupport status and result values shared across feature surfaces.
    namespace Types
    {
        /// ------------------------------------------------------------
        /// Shared operation results
        /// ------------------------------------------------------------

        /// @brief Shared portable operation status.
        using Status = GameWIP::Status::Types::Status;
        /// @brief Shared result type for a value-bearing operation.
        template <typename T> using Result = GameWIP::Status::Types::Result<T>;
        /// @brief Result containing UTF-8 text.
        using TextResult = GameWIP::Status::Types::Result<std::string>;
        /// @brief Result containing a boolean value.
        using BoolResult = GameWIP::Status::Types::Result<bool>;
        /// @brief Result containing a count.
        using CountResult = GameWIP::Status::Types::Result<std::size_t>;
    } // namespace Types

    /// ------------------------------------------------------------
    /// Status formatting
    /// ------------------------------------------------------------

    /// @brief Formats a shared operation status for TestSupport reports.
    /// @return Stable status-code name plus any native diagnostic.
    /// @note Formatting is performed only when requested and may allocate.
    [[nodiscard]] std::string formatStatus(const Types::Status &status);
} // namespace GameWIP::TestSupport
