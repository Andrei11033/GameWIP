/// @file win32_devices.cpp
/// @brief Win32 Raw Input device discovery and cleanup.

#include "input/platform/win32/internal/win32_attachment_state.h"
#include "input/platform/win32/internal/device_helpers.h"
#include "input/platform/win32/internal/device_metadata.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace GameWIP::Input::Platform::Win32
{
    /// ------------------------------------------------------------
    /// Raw Input device discovery
    /// ------------------------------------------------------------

    [[nodiscard]] bool addRawDevice(System::Impl &state, Attachment::Impl &attachment, DeviceRole role, HANDLE nativeDevice)
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

    [[nodiscard]] bool enumerateNativeDevices(System::Impl &state, Attachment::Impl &attachment)
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
    /// Raw Input cleanup
    /// ------------------------------------------------------------

    void unregisterRawInput(Attachment::Impl &attachment) noexcept
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
} // namespace GameWIP::Input::Platform::Win32
