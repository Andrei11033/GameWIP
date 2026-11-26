/// @file win32_devices.cpp
/// @brief Win32 Raw Input device discovery and cleanup.

#include "input/platform/win32/internal/win32_attachment_state.h"
#include "input/platform/win32/internal/device_helpers.h"
#include "input/platform/win32/internal/device_metadata.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <new>
#include <string>
#include <utility>
#include <vector>

namespace GameWIP::Input::Platform::Win32
{
    /// ------------------------------------------------------------
    /// Native input registration
    /// ------------------------------------------------------------

    [[nodiscard]] GameWIP::Status::Types::Status registerRawInputCollection(AttachmentState &attachment, USHORT usagePage, USHORT usage, DWORD flags)
    {
        const auto registered =
            std::find(attachment.rawInputCollections.begin(), attachment.rawInputCollections.end(), std::pair<USHORT, USHORT>{usagePage, usage});
        if(registered != attachment.rawInputCollections.end())
        {
            return GameWIP::Status::Types::Status{};
        }

        RAWINPUTDEVICE device{
            .usUsagePage = usagePage,
            .usUsage = usage,
            .dwFlags = flags,
            .hwndTarget = reinterpret_cast<HWND>(attachment.windowHandle),
        };
        if(!RegisterRawInputDevices(&device, 1, sizeof(device)))
        {
            return nativeStatus(GetLastError());
        }

        try
        {
            attachment.rawInputCollections.emplace_back(usagePage, usage);
            return GameWIP::Status::Types::Status{};
        }
        catch(const std::bad_alloc &)
        {
            RAWINPUTDEVICE removal{
                .usUsagePage = usagePage,
                .usUsage = usage,
                .dwFlags = RIDEV_REMOVE,
                .hwndTarget = nullptr,
            };
            (void)RegisterRawInputDevices(&removal, 1, sizeof(removal));
            return status(Code::OutOfMemory);
        }
    }

    [[nodiscard]] GameWIP::Status::Types::Status registerHidCollections(AttachmentState &attachment)
    {
        UINT deviceCount = 0;
        if(GetRawInputDeviceList(nullptr, &deviceCount, sizeof(RAWINPUTDEVICELIST)) == static_cast<UINT>(-1))
        {
            return nativeStatus(GetLastError());
        }

        if(deviceCount == 0)
        {
            return GameWIP::Status::Types::Status{};
        }

        std::vector<RAWINPUTDEVICELIST> devices(static_cast<std::size_t>(deviceCount));
        UINT received = deviceCount;
        if(GetRawInputDeviceList(devices.data(), &received, sizeof(RAWINPUTDEVICELIST)) == static_cast<UINT>(-1))
        {
            return nativeStatus(GetLastError());
        }

        for(std::size_t index = 0; index < static_cast<std::size_t>(received); ++index)
        {
            const RAWINPUTDEVICELIST &device = devices[index];
            if(device.dwType != RIM_TYPEHID)
            {
                continue;
            }

            RID_DEVICE_INFO info{};
            if(!hidDeviceInfo(device.hDevice, info) || info.hid.usUsagePage == HID_USAGE_PAGE_GENERIC)
            {
                continue;
            }

            const GameWIP::Status::Types::Status registrationStatus =
                registerRawInputCollection(attachment, info.hid.usUsagePage, info.hid.usUsage, RIDEV_DEVNOTIFY);
            if(!registrationStatus.ok())
            {
                return registrationStatus;
            }
        }

        return GameWIP::Status::Types::Status{};
    }

    [[nodiscard]] bool registerHidDeviceNotification(AttachmentState &attachment) noexcept
    {
        constexpr GUID hidInterfaceGuid{0x4D1E55B2, 0xF16F, 0x11CF, {0x88, 0xCB, 0x00, 0x11, 0x11, 0x00, 0x00, 0x30}};
        DEV_BROADCAST_DEVICEINTERFACE_W filter{};
        filter.dbcc_size = sizeof(filter);
        filter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
        filter.dbcc_classguid = hidInterfaceGuid;
        attachment.hidDeviceNotification =
            RegisterDeviceNotificationW(reinterpret_cast<HWND>(attachment.windowHandle), &filter, DEVICE_NOTIFY_WINDOW_HANDLE);
        return attachment.hidDeviceNotification != nullptr;
    }

    /// ------------------------------------------------------------
    /// ------------------------------------------------------------
    /// Raw Input device discovery
    /// ------------------------------------------------------------

    [[nodiscard]] bool addRawDevice(SystemState &state, AttachmentState &attachment, DeviceRole role, HANDLE nativeDevice)
    {
        if(nativeDevice == nullptr)
        {
            return false;
        }

        if(DeviceBinding *existing = findNativeDevice(attachment, role, nativeDevice); existing != nullptr)
        {
            reconnectDevice(state, *existing);
            return true;
        }
        const std::uint32_t slot = static_cast<std::uint32_t>(attachment.devices.size() + 1);
        const bool keyboard = role == DeviceRole::Keyboard;
        const std::string label = keyboard ? "Keyboard" : "Mouse";
        std::wstring nativePath;
        const std::string path = rawDeviceName(nativeDevice, &nativePath);
        const std::string pathKey = path.empty() ? std::string{} : "win32:device:path:" + path;
        const std::string containerKey = containerPersistentKey(nativePath);
        const std::string logicalKey = containerKey.empty() ? pathKey : containerKey;
        const Types::IdentityConfidence logicalConfidence =
            pathKey.empty() ? Types::IdentityConfidence::None
                            : (containerKey.empty() ? Types::IdentityConfidence::ConnectionPath : Types::IdentityConfidence::DeviceUnique);
        const HidDeviceStrings deviceStrings = queryHidDeviceStrings(nativePath);
        const std::string displayName = deviceStrings.product.empty() ? label : deviceStrings.product;

        addDevice(
            state,
            attachment,
            role,
            slot,
            displayName,
            deviceStrings.manufacturer,
            true,
            Types::SourceInfo{
                .backend = Types::BackendKind::Win32RawInput,
                .backendName = "Win32 Raw Input",
                .active = true,
                .persistentKey = pathKey,
                .identityConfidence = pathKey.empty() ? Types::IdentityConfidence::None : Types::IdentityConfidence::ConnectionPath,
            },
            logicalKey,
            logicalConfidence);
        DeviceBinding &binding = attachment.devices.back();
        binding.nativeDevice = nativeDevice;
        return true;
    }

    [[nodiscard]] bool enumerateNativeDevices(SystemState &state, AttachmentState &attachment)
    {
        UINT deviceCount = 0;
        if(GetRawInputDeviceList(nullptr, &deviceCount, sizeof(RAWINPUTDEVICELIST)) == static_cast<UINT>(-1))
        {
            return false;
        }

        if(deviceCount == 0)
        {
            return true;
        }
        std::vector<RAWINPUTDEVICELIST> devices(static_cast<std::size_t>(deviceCount));
        UINT received = deviceCount;
        if(GetRawInputDeviceList(devices.data(), &received, sizeof(RAWINPUTDEVICELIST)) == static_cast<UINT>(-1))
        {
            return false;
        }

        for(std::size_t index = 0; index < static_cast<std::size_t>(received); ++index)
        {
            const RAWINPUTDEVICELIST &device = devices[index];
            if(device.dwType == RIM_TYPEKEYBOARD)
            {
                if(attachment.rawInputEnabled)
                {
                    (void)addRawDevice(state, attachment, DeviceRole::Keyboard, device.hDevice);
                }
                continue;
            }

            if(device.dwType == RIM_TYPEMOUSE)
            {
                if(attachment.rawInputEnabled)
                {
                    (void)addRawDevice(state, attachment, DeviceRole::Mouse, device.hDevice);
                }
                continue;
            }

            if(device.dwType != RIM_TYPEHID || !attachment.hidEnabled)
            {
                continue;
            }

            if(!addHidDevice(state, attachment, device.hDevice))
            {
                (void)addOpaqueHidDevice(state, attachment, device.hDevice);
            }
        }
        return true;
    }

    /// ------------------------------------------------------------
    /// Input registration cleanup
    /// ------------------------------------------------------------

    void unregisterRawInput(AttachmentState &attachment) noexcept
    {
        for(const auto [usagePage, usage] : attachment.rawInputCollections)
        {
            RAWINPUTDEVICE device{
                .usUsagePage = usagePage,
                .usUsage = usage,
                .dwFlags = RIDEV_REMOVE,
                .hwndTarget = nullptr,
            };
            (void)RegisterRawInputDevices(&device, 1, sizeof(device));
        }

        attachment.rawInputCollections.clear();
    }

    void unregisterHidDeviceNotification(AttachmentState &attachment) noexcept
    {
        if(attachment.hidDeviceNotification != nullptr)
        {
            (void)UnregisterDeviceNotification(attachment.hidDeviceNotification);
            attachment.hidDeviceNotification = nullptr;
        }
    }
} // namespace GameWIP::Input::Platform::Win32
