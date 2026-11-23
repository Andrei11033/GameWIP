/// @file device_state.cpp
/// @brief Shared Win32 Input device state updates.

#include "input/platform/win32/internal/device_helpers.h"

#include <algorithm>

namespace GameWIP::Input::Platform::Win32
{
    /// ------------------------------------------------------------
    /// Device value and connection state
    /// ------------------------------------------------------------

    void appendElementEvent(
        System::Impl &state,
        Types::ElementId id,
        Types::Value value,
        Types::Value previous,
        bool previousKnown,
        Types::EventCause cause) noexcept
    {
        state.appendEvent({
            .timestamp = now(),
            .kind = Types::EventKind::ElementChanged,
            .cause = cause,
            .data =
                Types::ElementEvent{
                    .id = id,
                    .value = value,
                    .previousValue = previous,
                    .previousKnown = previousKnown,
                },
        });
    }

    [[nodiscard]] static Types::SourceInfo *sourceInfo(System::Impl &state, const DeviceBinding &device) noexcept
    {
        const std::size_t deviceIndex = state.findDeviceIndex(device.id);
        if(deviceIndex == state.devices.size() || device.sourceIndex >= state.devices[deviceIndex].sources.size())
        {
            return nullptr;
        }
        return &state.devices[deviceIndex].sources[device.sourceIndex];
    }

    [[nodiscard]] static bool hasActiveSource(const Types::DeviceInfo &device) noexcept
    {
        return std::any_of(
            device.sources.begin(),
            device.sources.end(),
            [](const Types::SourceInfo &source)
            {
                return source.active;
            });
    }

    [[nodiscard]] static bool hasActiveElementSource(const Attachment::Impl &attachment, Types::DeviceId device, Types::ElementId element) noexcept
    {
        return std::any_of(
            attachment.devices.begin(),
            attachment.devices.end(),
            [device, element](const DeviceBinding &candidate)
            {
                if(!candidate.connected || candidate.id != device)
                {
                    return false;
                }

                return std::any_of(
                    candidate.elements.begin(),
                    candidate.elements.end(),
                    [element](const ElementBinding &binding)
                    {
                        return binding.id == element;
                    });
            });
    }

    void invalidateDevice(System::Impl &state, Attachment::Impl &attachment, DeviceBinding &device, Types::EventCause cause) noexcept
    {
        if(!device.connected)
        {
            return;
        }
        const std::size_t deviceIndex = state.findDeviceIndex(device.id);
        if(deviceIndex == state.devices.size())
        {
            device.connected = false;
            return;
        }

        device.connected = false;
        if(Types::SourceInfo *source = sourceInfo(state, device); source != nullptr)
        {
            source->active = false;
        }

        Types::DeviceInfo &descriptor = state.devices[deviceIndex];
        descriptor.connected = hasActiveSource(descriptor);
        descriptor.descriptorRevision = ++state.deviceRevision;

        for(const Types::ElementInfo &element : descriptor.elements)
        {
            if(hasActiveElementSource(attachment, descriptor.id, element.id))
            {
                continue;
            }
            Types::ElementState *elementState = findState(state, element.id);
            if(elementState == nullptr)
            {
                continue;
            }
            elementState->validity = Types::ValueValidity::Unavailable;
            state.appendEvent({
                .timestamp = now(),
                .kind = Types::EventKind::ElementInvalidated,
                .cause = cause,
                .data =
                    Types::ElementEvent{
                        .id = element.id,
                        .value = elementState->value,
                        .previousValue = elementState->value,
                        .previousKnown = false,
                    },
            });
        }

        if(descriptor.connected)
        {
            appendDeviceEvent(state, Types::EventKind::DeviceChanged, cause, descriptor.id, descriptor.descriptorRevision);
            return;
        }

        if(state.health.activeDeviceCount != 0)
        {
            --state.health.activeDeviceCount;
        }

        appendDeviceEvent(state, Types::EventKind::DeviceDisconnected, cause, device.id, state.deviceRevision);
    }

    void reconnectDevice(System::Impl &state, DeviceBinding &device) noexcept
    {
        if(device.connected)
        {
            return;
        }
        const std::size_t deviceIndex = state.findDeviceIndex(device.id);
        if(deviceIndex == state.devices.size())
        {
            return;
        }
        Types::DeviceInfo &descriptor = state.devices[deviceIndex];
        const bool logicalWasConnected = descriptor.connected;
        device.connected = true;
        if(Types::SourceInfo *source = sourceInfo(state, device); source != nullptr)
        {
            source->active = true;
        }

        descriptor.connected = true;
        descriptor.descriptorRevision = ++state.deviceRevision;

        for(const ElementBinding &binding : device.elements)
        {
            Types::ElementState *elementState = findState(state, binding.id);
            if(elementState != nullptr && elementState->validity == Types::ValueValidity::Unavailable)
            {
                elementState->validity = Types::ValueValidity::Unknown;
            }
        }

        if(!logicalWasConnected)
        {
            ++state.health.activeDeviceCount;
            appendDeviceEvent(state, Types::EventKind::DeviceConnected, Types::EventCause::Resynchronized, device.id, descriptor.descriptorRevision);
        }
        else
        {
            appendDeviceEvent(state, Types::EventKind::DeviceChanged, Types::EventCause::SourceChanged, device.id, descriptor.descriptorRevision);
        }
    }
} // namespace GameWIP::Input::Platform::Win32
