/// @file device_lookup.cpp
/// @brief Shared Win32 Input device lookups.

#include "input/platform/win32/internal/device_helpers.h"

#include <algorithm>

namespace GameWIP::Input::Platform::Win32
{
    /// ------------------------------------------------------------
    /// Status and time helpers
    /// ------------------------------------------------------------

    [[nodiscard]] GameWIP::Status::Types::Status status(Code code) noexcept
    {
        return GameWIP::Status::Types::Status(code);
    }

    [[nodiscard]] GameWIP::Status::Types::Status nativeStatus(std::uint32_t code, GameWIP::Status::Types::NativeCodeDomain domain) noexcept
    {
        return GameWIP::Status::Types::Status::withDiagnostic(
            Code::NativeFailure,
            GameWIP::Status::Types::NativeCode{domain, static_cast<std::int64_t>(code)});
    }

    [[nodiscard]] Types::Timestamp now() noexcept
    {
        return Types::Timestamp{.receivedAt = Types::Clock::now()};
    }

    [[nodiscard]] std::uint64_t surfaceValue(const void *windowHandle) noexcept
    {
        const auto value = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(windowHandle));
        return value == 0 ? 1 : value;
    }

    /// ------------------------------------------------------------
    /// Device and element lookup
    /// ------------------------------------------------------------

    [[nodiscard]] Types::ElementState *findState(SystemState &state, Types::ElementId id) noexcept
    {
        const std::size_t index = state.findStateIndex(id);
        return index == state.states.size() ? nullptr : &state.states[index];
    }

    [[nodiscard]] Types::ElementInfo *findElement(SystemState &state, Types::ElementId id) noexcept
    {
        return state.findElement(id);
    }

    [[nodiscard]] DeviceBinding *findDevice(AttachmentState &attachment, DeviceRole role, std::uint32_t slot) noexcept
    {
        auto entry = std::find_if(
            attachment.devices.begin(),
            attachment.devices.end(),
            [role, slot](const DeviceBinding &device)
            {
                return device.role == role && device.nativeSlot == slot;
            });
        return entry == attachment.devices.end() ? nullptr : &(*entry);
    }

    [[nodiscard]] DeviceBinding *findNativeDevice(AttachmentState &attachment, DeviceRole role, void *nativeDevice) noexcept
    {
        auto entry = std::find_if(
            attachment.devices.begin(),
            attachment.devices.end(),
            [role, nativeDevice](const DeviceBinding &device)
            {
                return device.role == role && device.nativeDevice == nativeDevice;
            });
        return entry == attachment.devices.end() ? nullptr : &(*entry);
    }

    [[nodiscard]] DeviceBinding *findHidDevice(AttachmentState &attachment, void *nativeDevice) noexcept
    {
        return findNativeDevice(attachment, DeviceRole::Hid, nativeDevice);
    }

    [[nodiscard]] ElementBinding *findHidElement(DeviceBinding &device, Types::HidControlId hid) noexcept
    {
        auto entry = std::find_if(
            device.elements.begin(),
            device.elements.end(),
            [hid](const ElementBinding &element)
            {
                return element.hid == hid;
            });
        return entry == device.elements.end() ? nullptr : &(*entry);
    }

    [[nodiscard]] ElementBinding *findRole(DeviceBinding &device, ElementRole role) noexcept
    {
        auto entry = std::find_if(
            device.elements.begin(),
            device.elements.end(),
            [role](const ElementBinding &element)
            {
                return element.role == role;
            });
        return entry == device.elements.end() ? nullptr : &(*entry);
    }

    /// ------------------------------------------------------------
    /// Attachment validation
    /// ------------------------------------------------------------

    [[nodiscard]] GameWIP::Status::Types::Status checkAttached(const AttachmentState &attachment) noexcept
    {
        if(!attachment.attached || attachment.system == nullptr)
        {
            return status(Code::NotOpen);
        }

        if(!attachment.system->isOpen())
        {
            return status(Code::NotOpen);
        }
        return attachment.system->ownedByCurrentThread() ? GameWIP::Status::Types::Status{} : status(Code::ResourceBusy);
    }

    /// ------------------------------------------------------------
    /// Standard control lookup
    /// ------------------------------------------------------------

    [[nodiscard]] ElementBinding *standardElement(DeviceBinding &device, Types::StandardId standard) noexcept
    {
        const auto entry = std::find_if(
            device.elements.begin(),
            device.elements.end(),
            [standard](const ElementBinding &element)
            {
                return element.standard == standard;
            });
        return entry == device.elements.end() ? nullptr : &(*entry);
    }
} // namespace GameWIP::Input::Platform::Win32
