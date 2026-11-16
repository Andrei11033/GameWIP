/// @file device_metadata.cpp
/// @brief Win32 device identity and descriptor queries.

#include "input/platform/win32/internal/device_metadata.h"
#include "input/platform/win32/internal/win32_unicode.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <new>
#include <string>
#include <string_view>
#include <vector>

#include <setupapi.h>

namespace GameWIP::Input::Platform::Win32::Detail
{
    /// ------------------------------------------------------------
    /// Device names and descriptor data
    /// ------------------------------------------------------------

    [[nodiscard]] std::string rawDeviceName(HANDLE nativeDevice, std::wstring *outNativePath)
    {
        // Query the path size, then read the complete native device path.
        UINT characterCount = 0;
        if(GetRawInputDeviceInfoW(nativeDevice, RIDI_DEVICENAME, nullptr, &characterCount) == static_cast<UINT>(-1) || characterCount == 0)
        {
            return {};
        }
        std::vector<wchar_t> buffer(static_cast<std::size_t>(characterCount) + 1, L'\0');
        UINT received = characterCount;
        if(GetRawInputDeviceInfoW(nativeDevice, RIDI_DEVICENAME, buffer.data(), &received) == static_cast<UINT>(-1) || received == 0 ||
           received > buffer.size())
        {
            return {};
        }

        while(received != 0 && buffer[received - 1] == L'\0')
        {
            --received;
        }

        if(received == 0)
        {
            return {};
        }

        const std::wstring nativePath(buffer.data(), received);
        if(outNativePath != nullptr)
        {
            *outNativePath = nativePath;
        }

        return wideToUtf8(nativePath);
    }

    [[nodiscard]] std::string containerPersistentKey(std::wstring_view nativePath) noexcept
    {
        if(nativePath.empty())
        {
            return {};
        }

        try
        {
            // Resolve the device interface and its container identity.
            const HDEVINFO deviceInfoSet = SetupDiGetClassDevsW(nullptr, nullptr, nullptr, DIGCF_ALLCLASSES | DIGCF_DEVICEINTERFACE);
            if(deviceInfoSet == INVALID_HANDLE_VALUE)
            {
                return {};
            }

            struct DeviceInfoSetGuard
            {
                HDEVINFO value;
                ~DeviceInfoSetGuard()
                {
                    (void)SetupDiDestroyDeviceInfoList(value);
                }
            } guard{deviceInfoSet};

            const std::wstring path(nativePath);
            SP_DEVICE_INTERFACE_DATA interfaceData{};
            interfaceData.cbSize = sizeof(interfaceData);
            if(!SetupDiOpenDeviceInterfaceW(deviceInfoSet, path.c_str(), 0, &interfaceData))
            {
                return {};
            }

            DWORD detailSize = 0;
            SP_DEVINFO_DATA deviceData{};
            deviceData.cbSize = sizeof(deviceData);
            (void)SetupDiGetDeviceInterfaceDetailW(deviceInfoSet, &interfaceData, nullptr, 0, &detailSize, nullptr);
            if(GetLastError() != ERROR_INSUFFICIENT_BUFFER || detailSize < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W))
            {
                return {};
            }

            std::vector<std::byte> detailBuffer(detailSize);
            auto *detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W *>(detailBuffer.data());
            detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
            deviceData = {};
            deviceData.cbSize = sizeof(deviceData);
            if(!SetupDiGetDeviceInterfaceDetailW(deviceInfoSet, &interfaceData, detail, detailSize, nullptr, &deviceData))
            {
                return {};
            }

            // Read the container ID shared by interfaces on the physical device.
            constexpr DEVPROPKEY kContainerIdKey{
                {0x8C7ED206, 0x3F8A, 0x4827, {0xB3, 0xAB, 0xAE, 0x9E, 0x1F, 0xAE, 0xFC, 0x6C}},
                2,
            };
            GUID containerId{};
            DEVPROPTYPE propertyType = 0;
            DWORD propertySize = 0;
            if(!SetupDiGetDevicePropertyW(
                   deviceInfoSet,
                   &deviceData,
                   &kContainerIdKey,
                   &propertyType,
                   reinterpret_cast<PBYTE>(&containerId),
                   sizeof(containerId),
                   &propertySize,
                   0) ||
               propertyType != DEVPROP_TYPE_GUID || propertySize != sizeof(containerId) ||
               (containerId.Data1 == 0 && containerId.Data2 == 0 && containerId.Data3 == 0 &&
                std::ranges::all_of(
                    containerId.Data4,
                    [](BYTE byte)
                    {
                        return byte == 0;
                    })))
            {
                return {};
            }

            // Format the container ID as a stable grouping key.
            return std::format(
                "win32:container:{:08X}-{:04X}-{:04X}-{:02X}{:02X}-{:02X}{:02X}{:02X}{:02X}{:02X}{:02X}",
                static_cast<unsigned int>(containerId.Data1),
                static_cast<unsigned int>(containerId.Data2),
                static_cast<unsigned int>(containerId.Data3),
                static_cast<unsigned int>(containerId.Data4[0]),
                static_cast<unsigned int>(containerId.Data4[1]),
                static_cast<unsigned int>(containerId.Data4[2]),
                static_cast<unsigned int>(containerId.Data4[3]),
                static_cast<unsigned int>(containerId.Data4[4]),
                static_cast<unsigned int>(containerId.Data4[5]),
                static_cast<unsigned int>(containerId.Data4[6]),
                static_cast<unsigned int>(containerId.Data4[7]));
        }
        catch(...)
        {
            return {};
        }
    }

    namespace
    {
        /// ------------------------------------------------------------
        /// HID string query
        /// ------------------------------------------------------------

        [[nodiscard]] std::string queryHidString(HANDLE collection, auto query)
        {
            std::array<wchar_t, 2046> buffer{};
            if(query(collection, buffer.data(), static_cast<ULONG>(sizeof(buffer))) == FALSE)
            {
                return {};
            }

            const auto terminator = std::find(buffer.begin(), buffer.end(), L'\0');
            if(terminator == buffer.begin())
            {
                return {};
            }

            const std::size_t length = static_cast<std::size_t>(terminator - buffer.begin());
            return wideToUtf8(std::wstring_view(buffer.data(), length));
        }
    } // namespace

    [[nodiscard]] HidDeviceStrings queryHidDeviceStrings(std::wstring_view nativePath) noexcept
    {
        if(nativePath.empty())
        {
            return {};
        }

        try
        {
            // Open the collection and read its optional product strings.
            const std::wstring path(nativePath);
            const HANDLE collection =
                CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if(collection == INVALID_HANDLE_VALUE)
            {
                return {};
            }

            struct HandleGuard
            {
                HANDLE value;
                ~HandleGuard()
                {
                    (void)CloseHandle(value);
                }
            } guard{collection};

            return HidDeviceStrings{
                .product = queryHidString(collection, HidD_GetProductString),
                .manufacturer = queryHidString(collection, HidD_GetManufacturerString),
            };
        }
        catch(const std::bad_alloc &)
        {
            return {};
        }
    }

    [[nodiscard]] std::string hidDeviceFallbackName(bool hasNativeInfo, USAGE usagePage, USAGE usage)
    {
        if(!hasNativeInfo)
        {
            return "Opaque HID Device";
        }
        return "HID Device " + std::to_string(usagePage) + ":" + std::to_string(usage);
    }

    [[nodiscard]] bool isGenericDeviceName(std::string_view name) noexcept
    {
        return name.empty() || name == "Keyboard" || name == "Mouse" || name == "Opaque HID Device" || name.starts_with("HID Device ");
    }

    [[nodiscard]] bool hidDeviceInfo(HANDLE nativeDevice, RID_DEVICE_INFO &outInfo) noexcept
    {
        outInfo = {};
        outInfo.cbSize = sizeof(outInfo);
        UINT size = sizeof(outInfo);
        return GetRawInputDeviceInfoW(nativeDevice, RIDI_DEVICEINFO, &outInfo, &size) != static_cast<UINT>(-1) && outInfo.dwType == RIM_TYPEHID;
    }

    [[nodiscard]] bool hidPreparsedData(HANDLE nativeDevice, std::vector<std::byte> &outPreparsedData, HIDP_CAPS &outCaps) noexcept
    {
        // Query and read the descriptor before parsing its capabilities.
        UINT size = 0;
        if(GetRawInputDeviceInfoW(nativeDevice, RIDI_PREPARSEDDATA, nullptr, &size) == static_cast<UINT>(-1) || size == 0)
        {
            return false;
        }

        try
        {
            outPreparsedData.resize(size);
        }
        catch(const std::bad_alloc &)
        {
            return false;
        }

        UINT received = size;
        if(GetRawInputDeviceInfoW(nativeDevice, RIDI_PREPARSEDDATA, outPreparsedData.data(), &received) == static_cast<UINT>(-1) || received != size)
        {
            outPreparsedData.clear();
            return false;
        }

        outCaps = {};
        const auto preparsed = reinterpret_cast<PHIDP_PREPARSED_DATA>(outPreparsedData.data());
        return HidP_GetCaps(preparsed, &outCaps) == HIDP_STATUS_SUCCESS;
    }
} // namespace GameWIP::Input::Platform::Win32::Detail
