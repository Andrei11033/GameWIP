/// @file win32_compat.h
/// @brief Private compatibility declarations for modern Windows SDK additions.

#pragma once

#include <dwmapi.h>
#include <sdkddkver.h>

namespace GameWIP::Window::Detail::Platform::Compat
{
    /// Official DWMWA_REDIRECTIONBITMAP_ALPHA for DwmSetWindowAttribute.
    /// It requires Windows 11 version 24H2 (build 26100). Older supported SDKs
    /// and MinGW omit the enumerator; value 39 is from the current Windows SDK
    /// dwmapi.h. Declaration availability does not replace runtime build gating.
#if defined(_MSC_VER) && defined(NTDDI_WIN11_GE)
    inline constexpr DWMWINDOWATTRIBUTE kRedirectionBitmapAlpha = DWMWA_REDIRECTIONBITMAP_ALPHA;
    static_assert(static_cast<int>(kRedirectionBitmapAlpha) == 39);
#else
    inline constexpr DWMWINDOWATTRIBUTE kRedirectionBitmapAlpha = static_cast<DWMWINDOWATTRIBUTE>(39);
#endif
} // namespace GameWIP::Window::Detail::Platform::Compat
