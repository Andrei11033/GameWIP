/// @file win32_dialog_helpers.h
/// @brief Shared private helpers for Desktop's Win32 dialog backends.

#pragma once

#include "desktop/window.h"
#include "io/status.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <string>
#include <string_view>

namespace GameWIP::Desktop::Platform::Win32
{
    /// ------------------------------------------------------------
    /// Dialog helpers
    /// ------------------------------------------------------------

    /// @brief Resolves an optional portable owner to its current native HWND.
    [[nodiscard]] HWND ownerHandle(Window *owner) noexcept;

    /// @brief Converts UTF-8 dialog text without allowing exceptions across the API boundary.
    [[nodiscard]] IO::Types::Status convertText(std::string_view text, std::wstring &output) noexcept;
} // namespace GameWIP::Desktop::Platform::Win32
