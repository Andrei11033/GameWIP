/// @file process_arguments.h
/// @brief Bounded internal view over process-owned command-line arguments.

#pragma once

#include <cstddef>
#include <span>

namespace GameWIP::Validation
{
    using ProcessArguments = std::span<char *const>;

    /// @brief Converts the C++ entry-point argument pair into a borrowed bounded view.
    /// @note The language runtime owns argv and guarantees argc accessible entries for the process lifetime.
    [[nodiscard]] inline ProcessArguments processArguments(int argc, char **argv) noexcept
    {
        if (argc <= 0 || argv == nullptr)
            return {};
#if defined(__clang__)
#pragma clang unsafe_buffer_usage begin
#endif
        const ProcessArguments arguments(argv, static_cast<std::size_t>(argc));
#if defined(__clang__)
#pragma clang unsafe_buffer_usage end
#endif
        return arguments;
    }
} // namespace GameWIP::Validation
