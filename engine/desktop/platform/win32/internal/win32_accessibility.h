/// @file win32_accessibility.h
/// @brief Private UI Automation message integration.
#pragma once
#include "desktop/platform/win32/internal/win32_window_backend.h"
namespace GameWIP::Desktop::Detail::Platform
{
    [[nodiscard]] bool accessibilityGetObject(WindowState &state, HWND hwnd, WPARAM wParam, LPARAM lParam, LRESULT &out) noexcept;
    void accessibilityWindowDestroyed(WindowState &state, HWND hwnd) noexcept;
} // namespace GameWIP::Desktop::Detail::Platform
