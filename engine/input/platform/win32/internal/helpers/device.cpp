/// @file device.cpp
/// @brief Shared Win32 device registration helpers.

#include "input/platform/win32/internal/device_helpers.h"

#include <string>

namespace GameWIP::Input::Platform::Win32::Detail
{
    /// ------------------------------------------------------------
    /// Logical device registration
    /// ------------------------------------------------------------

    void appendDeviceEvent(
        System::Impl &state,
        Types::EventKind kind,
        Types::EventCause cause,
        Types::DeviceId device,
        std::uint64_t revision) noexcept
    {
        state.appendEvent({
            .timestamp = now(),
            .kind = kind,
            .cause = cause,
            .data = Types::DeviceEvent{device, revision},
        });
    }

    [[nodiscard]] Types::DeviceInfo *findLogicalDevice(System::Impl &state, std::string_view persistentKey) noexcept
    {
        if(persistentKey.empty())
        {
            return nullptr;
        }

        for(Types::DeviceInfo &device : state.devices)
        {
            if(static_cast<unsigned int>(device.identityConfidence) >= static_cast<unsigned int>(Types::IdentityConfidence::ConnectionPath) &&
               device.persistentKey == persistentKey)
            {
                return &device;
            }
        }

        return nullptr;
    }

    void publishDeviceRegistration(
        System::Impl &state,
        const Types::DeviceInfo &descriptor,
        bool newLogicalDevice,
        bool logicalWasConnected,
        bool connected) noexcept
    {
        if(newLogicalDevice)
        {
            if(!connected)
            {
                return;
            }

            ++state.health.activeDeviceCount;
            appendDeviceEvent(state, Types::EventKind::DeviceConnected, Types::EventCause::Reported, descriptor.id, descriptor.descriptorRevision);
            return;
        }

        if(!logicalWasConnected && connected)
        {
            ++state.health.activeDeviceCount;
            appendDeviceEvent(
                state,
                Types::EventKind::DeviceConnected,
                Types::EventCause::Resynchronized,
                descriptor.id,
                descriptor.descriptorRevision);
            return;
        }

        appendDeviceEvent(state, Types::EventKind::DeviceChanged, Types::EventCause::SourceChanged, descriptor.id, descriptor.descriptorRevision);
    }

    /// ------------------------------------------------------------
    /// Element descriptors
    /// ------------------------------------------------------------

    [[nodiscard]] std::string standardKey(Types::StandardId standard)
    {
        return "standard:" + std::to_string(static_cast<unsigned int>(standard.domain)) + ":" + std::to_string(standard.code);
    }

    [[nodiscard]] Types::NumericInfo numericInfo(
        Types::NumericStorage storage,
        std::optional<Types::Scalar> minimum,
        std::optional<Types::Scalar> maximum,
        Types::Unit unit) noexcept
    {
        return Types::NumericInfo{
            .nativeStorage = storage,
            .minimum = minimum,
            .maximum = maximum,
            .unit = unit,
        };
    }

    [[nodiscard]] Types::ComponentInfo component(std::uint8_t index, std::string_view name, Types::NumericInfo numeric)
    {
        return Types::ComponentInfo{
            .index = index,
            .name = std::string(name),
            .numeric = numeric,
        };
    }
} // namespace GameWIP::Input::Platform::Win32::Detail
