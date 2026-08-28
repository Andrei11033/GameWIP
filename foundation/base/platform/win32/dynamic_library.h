/// @file dynamic_library.h
/// @brief Typed boundary for resolving Win32 procedures from dynamic libraries.

#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <type_traits>

namespace GameWIP::Base::Win32
{
    /// @brief Resolves a Win32 procedure while preserving its exact function-pointer type.
    /// @tparam FunctionType Exact function-pointer type expected by the caller.
    /// @param module Borrowed loaded-module handle that remains owned by the caller.
    /// @param name Null-terminated exported procedure name.
    /// @return The typed procedure, or null when the module, name, or lookup result is null.
    /// @warning The returned pointer remains usable only while the module stays loaded.
    template <typename FunctionType>
        requires std::is_pointer_v<FunctionType> && std::is_function_v<std::remove_pointer_t<FunctionType>>
    [[nodiscard]] FunctionType loadProcedure(HMODULE module, const char *name) noexcept
    {
        if (module == nullptr || name == nullptr)
        {
            return nullptr;
        }

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"
#endif
        const auto function = reinterpret_cast<FunctionType>(GetProcAddress(module, name));
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
        return function;
    }
} // namespace GameWIP::Base::Win32
