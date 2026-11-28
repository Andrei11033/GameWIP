/// @file synthetic.cpp
/// @brief Implements the deterministic virtual-device Input producer.

#include "input/internal/system_state.h"
#include "input/internal/results.h"
#include "input/internal/value.h"
#include "input/synthetic.h"

#include "unicode/unicode.h"

#include <algorithm>
#include <new>
#include <utility>

namespace GameWIP::Input::Synthetic
{
    struct Source::Impl
    {
        System *system = nullptr;
        std::weak_ptr<void> systemLifetime{};
        std::uint64_t systemInstance = 0;
        bool attached = false;
        std::vector<Types::DeviceId> devices{};
    };

    namespace
    {
        using GameWIP::Status::Types::Code;

        [[nodiscard]] GameWIP::Status::Types::Status status(Code code) noexcept
        {
            return GameWIP::Status::Types::Status(code);
        }

        template <typename SourceState> [[nodiscard]] bool ownsDevice(const SourceState &source, Types::DeviceId id) noexcept
        {
            return std::find(source.devices.begin(), source.devices.end(), id) != source.devices.end();
        }

        [[nodiscard]] Types::Timestamp timestampOrNow(Types::Timestamp timestamp) noexcept
        {
            if(timestamp.receivedAt == Types::Clock::time_point{})
            {
                timestamp.receivedAt = Types::Clock::now();
            }
            return timestamp;
        }

        [[nodiscard]] Types::ComponentInfo defaultComponent(std::size_t index, std::string_view name)
        {
            return {
                .index = static_cast<std::uint8_t>(index),
                .name = std::string(name),
            };
        }
    } // namespace

    /// ------------------------------------------------------------
    /// Source attachment lifecycle
    /// ------------------------------------------------------------

    Source::Source() noexcept
        : impl_(std::unique_ptr<Impl>(new (std::nothrow) Impl{}))
    {
    }

    Source::~Source() noexcept
    {
        if(impl_ != nullptr)
        {
            (void)detach();
        }
    }

    GameWIP::Status::Types::Result<void> Source::attach(System &system) noexcept
    {
        return Internal::result(attachStatus(system));
    }

    GameWIP::Status::Types::Result<void> Source::detach() noexcept
    {
        return Internal::result(detachStatus());
    }

    GameWIP::Status::Types::Result<Types::DeviceId> Source::addDevice(const Types::SyntheticDeviceDescription &description) noexcept
    {
        Types::DeviceId device{};
        GameWIP::Status::Types::Status operationStatus = addDeviceStatus(description, device);
        if(!operationStatus.ok())
        {
            return Internal::failedResult<Types::DeviceId>(std::move(operationStatus));
        }
        return GameWIP::Status::Types::Result<Types::DeviceId>::success(device);
    }

    GameWIP::Status::Types::Result<void> Source::removeDevice(Types::DeviceId device) noexcept
    {
        return Internal::result(removeDeviceStatus(device));
    }

    GameWIP::Status::Types::Result<void> Source::setValue(Types::ElementId element, const Types::Value &value, Types::Timestamp when) noexcept
    {
        return Internal::result(setValueStatus(element, value, when));
    }

    GameWIP::Status::Types::Result<void> Source::setAbsoluteValue(Types::ElementId element, const Types::Value &value, Types::Timestamp when) noexcept
    {
        return Internal::result(setAbsoluteValueStatus(element, value, when));
    }

    GameWIP::Status::Types::Result<void> Source::addRelativeDelta(Types::ElementId element, const Types::Value &delta, Types::Timestamp when) noexcept
    {
        return Internal::result(addRelativeDeltaStatus(element, delta, when));
    }

    GameWIP::Status::Types::Result<void> Source::repeat(Types::ElementId element, Types::Timestamp when) noexcept
    {
        return Internal::result(repeatStatus(element, when));
    }

    GameWIP::Status::Types::Result<void> Source::invalidate(Types::ElementId element, Types::Timestamp when) noexcept
    {
        return Internal::result(invalidateStatus(element, when));
    }

    GameWIP::Status::Types::Result<void> Source::commitText(Types::SurfaceId surface, std::string_view utf8, Types::Timestamp when) noexcept
    {
        return Internal::result(commitTextStatus(surface, utf8, when));
    }

    GameWIP::Status::Types::Status Source::checkAttached() const noexcept
    {
        // Validate the source and session lifetime.
        if(impl_ == nullptr || !impl_->attached || impl_->system == nullptr)
        {
            return status(Code::NotOpen);
        }
        const std::shared_ptr<void> systemLifetime = impl_->systemLifetime.lock();
        if(systemLifetime == nullptr)
        {
            return status(Code::NotOpen);
        }

        // Reject a replaced session before checking thread ownership.
        if(!impl_->system->isOpen() || impl_->system->impl_ == nullptr || impl_->system->impl_->instance != impl_->systemInstance)
        {
            return status(Code::NotOpen);
        }

        if(impl_->system->impl_->sessionLifetime != systemLifetime)
        {
            return status(Code::NotOpen);
        }
        return impl_->system->ownedByCurrentThread() ? GameWIP::Status::Types::Status{} : status(Code::ResourceBusy);
    }

    GameWIP::Status::Types::Status Source::attachStatus(System &system) noexcept
    {
        if(impl_ == nullptr)
        {
            return status(Code::OutOfMemory);
        }

        // Clear stale ownership before attaching to another session.
        if(impl_->attached)
        {
            const GameWIP::Status::Types::Status attachedStatus = checkAttached();
            if(attachedStatus.ok())
            {
                return impl_->system == &system ? status(Code::AlreadyOpen) : status(Code::ResourceBusy);
            }

            if(attachedStatus.code() != Code::NotOpen)
            {
                return attachedStatus;
            }
            impl_->system = nullptr;
            impl_->systemLifetime.reset();
            impl_->systemInstance = 0;
            impl_->attached = false;
            impl_->devices.clear();
        }

        // Validate the target session and its owner thread.
        if(!system.isOpen())
        {
            return status(Code::NotOpen);
        }

        if(!system.ownedByCurrentThread())
        {
            return status(Code::ResourceBusy);
        }

        // Bind the source to the current session lifetime.
        impl_->system = &system;
        impl_->systemLifetime = system.impl_->sessionLifetime;
        impl_->systemInstance = system.impl_->instance;
        impl_->attached = true;
        return GameWIP::Status::Types::Status{};
    }

    GameWIP::Status::Types::Status Source::detachStatus() noexcept
    {
        if(impl_ == nullptr || !impl_->attached)
        {
            return GameWIP::Status::Types::Status{};
        }

        const GameWIP::Status::Types::Status attachedStatus = checkAttached();
        if(!attachedStatus.ok())
        {
            if(attachedStatus.code() == Code::NotOpen)
            {
                // The session is gone, so discard this source's stale binding.
                impl_->system = nullptr;
                impl_->systemLifetime.reset();
                impl_->systemInstance = 0;
                impl_->attached = false;
                impl_->devices.clear();
                return GameWIP::Status::Types::Status{};
            }
            return attachedStatus;
        }

        // Remove owned devices before clearing the live session binding.
        while(!impl_->devices.empty())
        {
            const GameWIP::Status::Types::Status removeStatus = removeDeviceStatus(impl_->devices.back());
            if(!removeStatus.ok())
            {
                return removeStatus;
            }
        }

        impl_->system = nullptr;
        impl_->systemLifetime.reset();
        impl_->systemInstance = 0;
        impl_->attached = false;
        return GameWIP::Status::Types::Status{};
    }

    /// ------------------------------------------------------------
    /// Synthetic device lifecycle
    /// ------------------------------------------------------------

    GameWIP::Status::Types::Status Source::addDeviceStatus(const Types::SyntheticDeviceDescription &description, Types::DeviceId &outDevice) noexcept
    {
        outDevice = {};

        // Validate the source and top-level descriptor.
        if(impl_ == nullptr)
        {
            return status(Code::OutOfMemory);
        }

        const GameWIP::Status::Types::Status attachedStatus = checkAttached();
        if(!attachedStatus.ok())
        {
            return attachedStatus;
        }

        if(description.name.empty() || description.persistentKey.empty() || description.elements.empty())
        {
            return status(Code::InvalidArgument);
        }

        // Validate each element's identity, value layout, and key uniqueness.
        for(const Types::SyntheticElementDescription &descriptionElement : description.elements)
        {
            if(descriptionElement.name.empty() || descriptionElement.persistentKey.empty() || descriptionElement.kind == Types::ValueKind::None ||
               (descriptionElement.identity.standard.has_value() && !descriptionElement.identity.standard->isValid()) ||
               (descriptionElement.identity.hid.has_value() && !descriptionElement.identity.hid->isValid()) ||
               (descriptionElement.reporting == Types::ReportingMode::Relative && descriptionElement.kind == Types::ValueKind::Boolean) ||
               (descriptionElement.reporting == Types::ReportingMode::Relative &&
                (descriptionElement.kind == Types::ValueKind::QuaternionF32 || descriptionElement.kind == Types::ValueKind::QuaternionF64 ||
                 descriptionElement.kind == Types::ValueKind::Spatial)) ||
               (descriptionElement.kind == Types::ValueKind::Boolean && !descriptionElement.components.empty()))
            {
                return status(Code::InvalidArgument);
            }

            if(!descriptionElement.components.empty())
            {
                // Require component metadata to match the value shape and index order.
                const std::size_t expectedCount = Internal::componentCount(descriptionElement.kind);
                if(descriptionElement.components.size() != expectedCount)
                {
                    return status(Code::InvalidArgument);
                }

                for(std::size_t index = 0; index < descriptionElement.components.size(); ++index)
                {
                    if(descriptionElement.components[index].index != index)
                    {
                        return status(Code::InvalidArgument);
                    }
                }
            }

            // Persistent element keys must be unique within this device.
            for(const Types::SyntheticElementDescription &other : description.elements)
            {
                if(&descriptionElement != &other && descriptionElement.persistentKey == other.persistentKey)
                {
                    return status(Code::InvalidArgument);
                }
            }
        }

        // Snapshot published state so allocation failures can roll back cleanly.
        Internal::SystemState &state = *impl_->system->impl_;

        // Record collection sizes.
        const std::size_t initialDeviceCount = state.devices.size();
        const std::size_t initialStateCount = state.states.size();
        const std::size_t initialSourceDeviceCount = impl_->devices.size();
        const std::size_t initialEventCount = state.pendingEvents.size();

        // Record identity and revision counters.
        const std::uint32_t initialDeviceSlot = state.nextDeviceSlot;
        const std::uint32_t initialElementSlot = state.nextElementSlot;
        const std::uint64_t initialDeviceRevision = state.deviceRevision;

        // Record pending event state and session health.
        const std::uint64_t initialDroppedEvents = state.pendingDroppedEvents;
        const bool initialDiscontinuity = state.pendingDiscontinuity;
        const Types::Health initialHealth = state.health;

        const auto rollback = [&] noexcept
        {
            // Restore published collections.
            state.devices.resize(initialDeviceCount);
            state.states.resize(initialStateCount);
            impl_->devices.resize(initialSourceDeviceCount);
            state.pendingEvents.resize(initialEventCount);

            // Restore identity and revision counters.
            state.nextDeviceSlot = initialDeviceSlot;
            state.nextElementSlot = initialElementSlot;
            state.deviceRevision = initialDeviceRevision;

            // Restore pending event state and health.
            state.pendingDroppedEvents = initialDroppedEvents;
            state.pendingDiscontinuity = initialDiscontinuity;
            state.health = initialHealth;
        };

        try
        {
            // Reserve destination storage before assigning device and element IDs.
            state.devices.reserve(state.devices.size() + 1);
            state.states.reserve(state.states.size() + description.elements.size());
            impl_->devices.reserve(impl_->devices.size() + 1);

            // Build the descriptor and initial unknown element states.
            const Types::DeviceId deviceId{state.instance, state.nextDeviceSlot++, 1};
            Types::DeviceInfo device{
                .id = deviceId,
                .origin = Types::DeviceOrigin::Virtual,
                .name = description.name,
                .manufacturer = "GameWIP",
                .persistentKey = description.persistentKey,
                .identityConfidence = Types::IdentityConfidence::ModelOnly,
                .connected = true,
                .descriptorRevision = ++state.deviceRevision,
                .sources = {{
                    .backend = Types::BackendKind::Synthetic,
                    .backendName = "Synthetic",
                    .active = true,
                }},
            };
            std::vector<Types::ElementState> newStates;
            newStates.reserve(description.elements.size());

            for(const Types::SyntheticElementDescription &descriptionElement : description.elements)
            {
                const Types::ElementId elementId{deviceId, state.nextElementSlot++, 1};
                Types::ElementInfo element{
                    .id = elementId,
                    .name = descriptionElement.name,
                    .persistentKey = descriptionElement.persistentKey,
                    .identity = descriptionElement.identity,
                    .kind = descriptionElement.kind,
                    .reporting = descriptionElement.reporting,
                    .components = descriptionElement.components,
                    .authoritativeBackend = Types::BackendKind::Synthetic,
                    .provenance = Types::ValueProvenance::Synthetic,
                };

                if(element.components.empty())
                {
                    const std::size_t count = Internal::componentCount(element.kind);
                    element.components.reserve(count);
                    for(std::size_t index = 0; index < count; ++index)
                    {
                        element.components.push_back(defaultComponent(index, element.name));
                    }
                }

                device.elements.push_back(std::move(element));
                newStates.push_back({
                    .id = elementId,
                    .reporting = descriptionElement.reporting,
                    .value = Internal::zeroValue(descriptionElement.kind),
                    .validity = Types::ValueValidity::Unknown,
                });
            }

            // Publish the descriptor and initial element states.
            state.devices.push_back(std::move(device));
            for(Types::ElementState &newState : newStates)
            {
                state.states.push_back(newState);
            }

            // Add source ownership and update the active device count.
            impl_->devices.push_back(deviceId);
            ++state.health.activeDeviceCount;

            state.appendEvent({
                .timestamp = timestampOrNow({}),
                .kind = Types::EventKind::DeviceConnected,
                .cause = Types::EventCause::Synthesized,
                .data = Types::DeviceEvent{deviceId, state.deviceRevision},
            });

            outDevice = deviceId;
            return GameWIP::Status::Types::Status{};
        }
        catch(const std::bad_alloc &)
        {
            rollback();
            return status(Code::OutOfMemory);
        }
    }

    GameWIP::Status::Types::Status Source::removeDeviceStatus(Types::DeviceId device) noexcept
    {
        if(impl_ == nullptr)
        {
            return status(Code::OutOfMemory);
        }

        const GameWIP::Status::Types::Status attachedStatus = checkAttached();
        if(!attachedStatus.ok())
        {
            return attachedStatus;
        }

        if(!ownsDevice(*impl_, device))
        {
            return status(Code::NotFound);
        }

        // Resolve the logical descriptor before invalidating its elements.
        Internal::SystemState &state = *impl_->system->impl_;
        const std::size_t deviceIndex = state.findDeviceIndex(device);
        if(deviceIndex == state.devices.size())
        {
            return status(Code::NotFound);
        }

        try
        {
            // Mark each current element unavailable before removing the device.
            for(const Types::ElementInfo &element : state.devices[deviceIndex].elements)
            {
                const std::size_t stateIndex = state.findStateIndex(element.id);
                if(stateIndex == state.states.size())
                {
                    continue;
                }

                Types::ElementState &elementState = state.states[stateIndex];
                elementState.validity = Types::ValueValidity::Unavailable;
                state.appendEvent({
                    .timestamp = timestampOrNow({}),
                    .kind = Types::EventKind::ElementInvalidated,
                    .cause = Types::EventCause::Disconnected,
                    .data =
                        Types::ElementEvent{
                            .id = element.id,
                            .value = elementState.value,
                            .previousValue = elementState.value,
                            .previousKnown = false,
                        },
                });
            }

            // Remove the descriptor and update the active device count.
            const std::uint64_t revision = ++state.deviceRevision;
            state.devices.erase(state.devices.begin() + static_cast<std::ptrdiff_t>(deviceIndex));

            if(state.health.activeDeviceCount != 0)
            {
                --state.health.activeDeviceCount;
            }

            // Publish the disconnect event and release source ownership.
            state.appendEvent({
                .timestamp = timestampOrNow({}),
                .kind = Types::EventKind::DeviceDisconnected,
                .cause = Types::EventCause::Disconnected,
                .data = Types::DeviceEvent{device, revision},
            });

            const auto owned = std::find(impl_->devices.begin(), impl_->devices.end(), device);
            impl_->devices.erase(owned);
            return GameWIP::Status::Types::Status{};
        }
        catch(const std::bad_alloc &)
        {
            return status(Code::OutOfMemory);
        }
    }

    /// ------------------------------------------------------------
    /// Synthetic state and event publication
    /// ------------------------------------------------------------

    GameWIP::Status::Types::Status Source::setValueStatus(Types::ElementId element, const Types::Value &value, Types::Timestamp when) noexcept
    {
        return publishValue(element, value, std::nullopt, when);
    }

    GameWIP::Status::Types::Status Source::setAbsoluteValueStatus(Types::ElementId element, const Types::Value &value, Types::Timestamp when) noexcept
    {
        return publishValue(element, value, Types::ReportingMode::Absolute, when);
    }

    GameWIP::Status::Types::Status Source::addRelativeDeltaStatus(Types::ElementId element, const Types::Value &delta, Types::Timestamp when) noexcept
    {
        return publishValue(element, delta, Types::ReportingMode::Relative, when);
    }

    GameWIP::Status::Types::Status Source::publishValue(
        Types::ElementId element,
        const Types::Value &value,
        std::optional<Types::ReportingMode> expectedReporting,
        Types::Timestamp when) noexcept
    {
        if(impl_ == nullptr)
        {
            return status(Code::OutOfMemory);
        }

        const GameWIP::Status::Types::Status attachedStatus = checkAttached();
        if(!attachedStatus.ok())
        {
            return attachedStatus;
        }

        // Resolve ownership, descriptor metadata, and current state.
        if(!ownsDevice(*impl_, element.device))
        {
            return status(Code::NotFound);
        }

        Internal::SystemState &state = *impl_->system->impl_;
        const Types::ElementInfo *info = state.findElement(element);
        const std::size_t stateIndex = state.findStateIndex(element);
        if(info == nullptr || stateIndex == state.states.size())
        {
            return status(Code::NotFound);
        }

        if(expectedReporting.has_value() && info->reporting != *expectedReporting)
        {
            return status(Code::InvalidArgument);
        }

        if(!Internal::hasValueKind(value, info->kind))
        {
            return status(Code::InvalidArgument);
        }

        try
        {
            // Resolve absolute values directly and accumulate relative values when the prior value is valid.
            Types::ElementState &elementState = state.states[stateIndex];
            const bool previousKnown = elementState.validity == Types::ValueValidity::Valid;
            const Types::Value previousValue = elementState.value;
            Types::Value nextValue = value;
            const bool relative = info->reporting == Types::ReportingMode::Relative;
            if(relative)
            {
                if(previousKnown)
                {
                    nextValue = elementState.value;
                    if(!Internal::addRelative(nextValue, value))
                    {
                        return status(Code::InvalidArgument);
                    }
                }

                if(!previousKnown)
                {
                    nextValue = value;
                }
            }

            // Track boolean transitions independently of the event payload.
            const bool *previousBool = previousKnown ? std::get_if<bool>(&previousValue) : nullptr;
            const bool *nextBool = std::get_if<bool>(&nextValue);
            if(nextBool != nullptr)
            {
                if(*nextBool && (previousBool == nullptr || !*previousBool))
                {
                    ++elementState.becameTrueCount;
                }
                else if(previousBool != nullptr && *previousBool && !*nextBool)
                {
                    ++elementState.becameFalseCount;
                }
            }

            // Store the accepted value and publish its change event.
            elementState.value = nextValue;
            elementState.validity = Types::ValueValidity::Valid;
            elementState.lastObservation = timestampOrNow(when);
            state.appendEvent({
                .timestamp = elementState.lastObservation,
                .kind = Types::EventKind::ElementChanged,
                .cause = Types::EventCause::Synthesized,
                .data =
                    Types::ElementEvent{
                        .id = element,
                        .value = relative ? value : elementState.value,
                        .previousValue = previousValue,
                        .previousKnown = previousKnown,
                    },
            });
            return GameWIP::Status::Types::Status{};
        }
        catch(const std::bad_alloc &)
        {
            return status(Code::OutOfMemory);
        }
    }

    GameWIP::Status::Types::Status Source::repeatStatus(Types::ElementId element, Types::Timestamp when) noexcept
    {
        if(impl_ == nullptr)
        {
            return status(Code::OutOfMemory);
        }

        const GameWIP::Status::Types::Status attachedStatus = checkAttached();
        if(!attachedStatus.ok())
        {
            return attachedStatus;
        }

        if(!ownsDevice(*impl_, element.device))
        {
            return status(Code::NotFound);
        }

        Internal::SystemState &state = *impl_->system->impl_;
        if(state.findElement(element) == nullptr)
        {
            return status(Code::NotFound);
        }

        const std::size_t stateIndex = state.findStateIndex(element);
        if(stateIndex == state.states.size() || state.states[stateIndex].validity != Types::ValueValidity::Valid)
        {
            return status(Code::InvalidArgument);
        }

        Types::ElementState &elementState = state.states[stateIndex];
        ++elementState.repeatCount;
        elementState.lastObservation = timestampOrNow(when);
        state.appendEvent({
            .timestamp = elementState.lastObservation,
            .kind = Types::EventKind::ElementRepeated,
            .cause = Types::EventCause::Synthesized,
            .data =
                Types::ElementEvent{
                    .id = element,
                    .value = elementState.value,
                    .previousValue = elementState.value,
                    .previousKnown = true,
                },
        });
        return GameWIP::Status::Types::Status{};
    }

    GameWIP::Status::Types::Status Source::invalidateStatus(Types::ElementId element, Types::Timestamp when) noexcept
    {
        if(impl_ == nullptr)
        {
            return status(Code::OutOfMemory);
        }

        const GameWIP::Status::Types::Status attachedStatus = checkAttached();
        if(!attachedStatus.ok())
        {
            return attachedStatus;
        }

        if(!ownsDevice(*impl_, element.device))
        {
            return status(Code::NotFound);
        }

        Internal::SystemState &state = *impl_->system->impl_;
        const std::size_t stateIndex = state.findStateIndex(element);
        if(state.findElement(element) == nullptr || stateIndex == state.states.size())
        {
            return status(Code::NotFound);
        }

        Types::ElementState &elementState = state.states[stateIndex];
        elementState.validity = Types::ValueValidity::Unavailable;
        elementState.lastObservation = timestampOrNow(when);
        state.appendEvent({
            .timestamp = elementState.lastObservation,
            .kind = Types::EventKind::ElementInvalidated,
            .cause = Types::EventCause::Synthesized,
            .data =
                Types::ElementEvent{
                    .id = element,
                    .value = elementState.value,
                    .previousValue = elementState.value,
                    .previousKnown = false,
                },
        });
        return GameWIP::Status::Types::Status{};
    }

    /// ------------------------------------------------------------
    /// Synthetic text publication
    /// ------------------------------------------------------------

    GameWIP::Status::Types::Status Source::commitTextStatus(Types::SurfaceId surface, std::string_view utf8, Types::Timestamp when) noexcept
    {
        if(impl_ == nullptr)
        {
            return status(Code::OutOfMemory);
        }

        const GameWIP::Status::Types::Status attachedStatus = checkAttached();
        if(!attachedStatus.ok())
        {
            return attachedStatus;
        }

        if(!impl_->system->impl_->textEnabled)
        {
            return status(Code::Unsupported);
        }

        if(surface.value == 0)
        {
            return status(Code::InvalidArgument);
        }

        if(Unicode::Utf8::validate(utf8).outcome != Unicode::Types::ValidationOutcome::Valid)
        {
            return status(Code::EncodingFailed);
        }

        // Enforce the pending-text byte limit before copying the payload.
        Internal::SystemState &state = *impl_->system->impl_;
        if(state.pendingTextUtf8.size() > state.configuration.maximumPendingTextBytes ||
           utf8.size() > state.configuration.maximumPendingTextBytes - state.pendingTextUtf8.size())
        {
            state.pendingDiscontinuity = true;
            return status(Code::SizeLimitExceeded);
        }

        try
        {
            Types::Event event{
                .timestamp = timestampOrNow(when),
                .kind = Types::EventKind::TextCommitted,
                .cause = Types::EventCause::Synthesized,
                .data =
                    Types::TextEvent{
                        .surface = surface,
                        .utf8 = std::string(utf8),
                    },
            };
            state.pendingTextUtf8.append(utf8);
            state.appendEvent(std::move(event));
            return GameWIP::Status::Types::Status{};
        }
        catch(const std::bad_alloc &)
        {
            return status(Code::OutOfMemory);
        }
    }
} // namespace GameWIP::Input::Synthetic
