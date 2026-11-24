/// @file process_arguments.h
/// @brief Bounded internal view over process-owned command-line arguments.

#pragma once

#include <cstddef>
#include <span>
#include <string_view>

namespace GameWIP::Validation
{
    using ProcessArguments = std::span<char *const>;

    /// @brief Converts the C++ entry-point argument pair into a borrowed bounded view.
    /// @note The language runtime owns @p argv and provides @p argc accessible argument pointers for the process lifetime.
    [[nodiscard]] inline ProcessArguments processArguments(int argc, char **argv) noexcept
    {
        if(argc <= 0 || argv == nullptr)
        {
            return {};
        }

#if defined(__clang__)
#pragma clang unsafe_buffer_usage begin
#endif
        const ProcessArguments arguments(argv, static_cast<std::size_t>(argc));
#if defined(__clang__)
#pragma clang unsafe_buffer_usage end
#endif
        return arguments;
    }

    /// @brief Checks for an exact option after the executable name.
    [[nodiscard]] inline bool hasProcessArgument(ProcessArguments arguments, std::string_view expected) noexcept
    {
        for(std::size_t index = 1; index < arguments.size(); ++index)
        {
            const char *value = arguments[index];

            if(value != nullptr && std::string_view(value) == expected)
            {
                return true;
            }
        }

        return false;
    }

    /// @brief Checks the process arguments for one exact option.
    [[nodiscard]] inline bool hasProcessArgument(int argc, char **argv, std::string_view expected) noexcept
    {
        return hasProcessArgument(processArguments(argc, argv), expected);
    }
} // namespace GameWIP::Validation
