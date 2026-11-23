/// @file win32_accessibility_text_helpers.h
/// @brief Shared private helpers for Win32 UI Automation text patterns.

#pragma once

#include "desktop/platform/win32/internal/win32_accessibility_provider.h"

namespace GameWIP::Desktop::Platform
{
    /// ------------------------------------------------------------
    /// Text-range allocation
    /// ------------------------------------------------------------

    [[nodiscard]] HRESULT makeTextRange(const UiaQuery &query, A::TextRange bounds, ITextRangeProvider **out) noexcept;
} // namespace GameWIP::Desktop::Platform
