/// @file device_metadata.h
/// @brief Private Win32 device identity and descriptor queries.

#pragma once

#include "input/platform/win32/internal/win32_attachment_state.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace GameWIP::Input::Platform::Win32
{
    struct HidDeviceStrings
    {
        std::string product;
        std::string manufacturer;
    };

    /// ------------------------------------------------------------
    /// Device identity and names
    /// ------------------------------------------------------------

    [[nodiscard]] std::string rawDeviceName(HANDLE nativeDevice, std::wstring *outNativePath = nullptr);
    [[nodiscard]] std::string containerPersistentKey(std::wstring_view nativePath) noexcept;
    [[nodiscard]] HidDeviceStrings queryHidDeviceStrings(std::wstring_view nativePath) noexcept;
    [[nodiscard]] bool isGenericDeviceName(std::string_view name) noexcept;

    /// ------------------------------------------------------------
    /// HID descriptor data
    /// ------------------------------------------------------------

    [[nodiscard]] std::string hidDeviceFallbackName(bool hasNativeInfo, USAGE usagePage, USAGE usage);
    [[nodiscard]] bool hidDeviceInfo(HANDLE nativeDevice, RID_DEVICE_INFO &outInfo) noexcept;
    [[nodiscard]] bool hidPreparsedData(HANDLE nativeDevice, std::vector<std::byte> &outPreparsedData, HIDP_CAPS &outCaps) noexcept;
} // namespace GameWIP::Input::Platform::Win32
