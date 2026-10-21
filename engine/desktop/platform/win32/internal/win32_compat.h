/// @file win32_compat.h
/// @brief Private Windows SDK declarations with runtime capability checks.

#pragma once

#include <dwmapi.h>
#include <sdkddkver.h>
#include <wingdi.h>

#include <cstdint>

namespace GameWIP::Desktop::Detail::Platform::Compat
{
    /// Official DWMWA_REDIRECTIONBITMAP_ALPHA for DwmSetWindowAttribute.
    /// It requires Windows 11 version 24H2 (build 26100). Supported SDKs
    /// can omit the enumerator; value 39 is from the Windows SDK
    /// dwmapi.h. Declaration availability does not replace runtime build gating.
#if defined(_MSC_VER) && defined(NTDDI_WIN11_GE)
    inline constexpr DWMWINDOWATTRIBUTE kRedirectionBitmapAlpha = DWMWA_REDIRECTIONBITMAP_ALPHA;
    static_assert(static_cast<int>(kRedirectionBitmapAlpha) == 39);
#else
    inline constexpr DWMWINDOWATTRIBUTE kRedirectionBitmapAlpha = static_cast<DWMWINDOWATTRIBUTE>(39);
#endif

    /// Documented Windows 11 advanced-color query declarations.
    /// MinGW can hide these declarations behind NTDDI guards. Runtime selection
    /// uses this query when available and DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO
    /// otherwise.
    enum class AdvancedColorMode : std::uint32_t
    {
        Sdr = 0,            ///< Standard dynamic range.
        WideColorGamut = 1, ///< Advanced-color SDR.
        Hdr = 2             ///< High dynamic range.
    };

    /// @brief ABI-compatible form of DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO_2.
    struct AdvancedColorInfo2
    {
        DISPLAYCONFIG_DEVICE_INFO_HEADER header;                                       ///< DisplayConfig query header.
        std::uint32_t flags = 0;                                                       ///< Native support and enablement bits.
        DISPLAYCONFIG_COLOR_ENCODING colorEncoding = DISPLAYCONFIG_COLOR_ENCODING_RGB; ///< Active wire encoding.
        std::uint32_t bitsPerColorChannel = 0;                                         ///< Active wire precision.
        AdvancedColorMode activeColorMode = AdvancedColorMode::Sdr;                    ///< Current advanced-color mode.
    };

    /// @brief Numeric query type used when the SDK omits its named enumerator.
    inline constexpr DISPLAYCONFIG_DEVICE_INFO_TYPE kGetAdvancedColorInfo2 = static_cast<DISPLAYCONFIG_DEVICE_INFO_TYPE>(15);
    /// @brief Native advanced-color-active flag.
    inline constexpr std::uint32_t kAdvancedColorActive = std::uint32_t{1} << 1U;
    /// @brief Windows 11 HDR-support flag.
    inline constexpr std::uint32_t kHighDynamicRangeSupported = std::uint32_t{1} << 4U;
    /// @brief Windows 11 HDR-enabled flag.
    inline constexpr std::uint32_t kHighDynamicRangeUserEnabled = std::uint32_t{1} << 5U;
    /// @brief Windows 11 wide-color-support flag.
    inline constexpr std::uint32_t kWideColorSupported = std::uint32_t{1} << 6U;
    /// @brief Windows 11 wide-color-enabled flag.
    inline constexpr std::uint32_t kWideColorUserEnabled = std::uint32_t{1} << 7U;
    static_assert(sizeof(AdvancedColorInfo2) == 36);
} // namespace GameWIP::Desktop::Detail::Platform::Compat
