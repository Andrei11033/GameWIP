/// @file synthetic.cpp
/// @brief Implements the deterministic virtual-device Input producer.

#include "input/internal/system_state.h"
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
        using IO::Types::ErrorCode;

        [[nodiscard]] IO::Types::Status status(ErrorCode code) noexcept
        {
            return IO::makeStatus(code);
        }

        template <typename SourceState> [[nodiscard]] bool ownsDevice(const SourceState &source, Types::DeviceId id) noexcept
        {
            return std::find(source.devices.begin(), source.devices.end(), id) != source.devices.end();
        }

        [[nodiscard]] std::size_t componentCount(Types::ValueKind kind) noexcept
        {
            switch(kind)
            {
            case Types::ValueKind::Vector2I64:
            case Types::ValueKind::Vector2U64:
            case Types::ValueKind::Vector2F32:
            case Types::ValueKind::Vector2F64:
                return 2;
            case Types::ValueKind::Vector3I64:
            case Types::ValueKind::Vector3U64:
            case Types::ValueKind::Vector3F32:
            case Types::ValueKind::Vector3F64:
                return 3;
            case Types::ValueKind::QuaternionF32:
            case Types::ValueKind::QuaternionF64:
                return 4;
            case Types::ValueKind::Spatial:
                return 7;
            case Types::ValueKind::None:
            case Types::ValueKind::Boolean:
                return 0;
            case Types::ValueKind::SignedInteger:
            case Types::ValueKind::UnsignedInteger:
            case Types::ValueKind::Float32:
            case Types::ValueKind::Float64:
                return 1;
            }

            return 0;
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

    // ------------------------------------------------------------
    // Source attachment lifecycle
    // ------------------------------------------------------------
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

    IO::Types::Status Source::checkAttached() const noexcept
    {
        if(impl_ == nullptr || !impl_->attached || impl_->system == nullptr)
        {
            return status(ErrorCode::NotOpen);
        }
        const std::shared_ptr<void> systemLifetime = impl_->systemLifetime.lock();
        if(systemLifetime == nullptr)
        {
            return status(ErrorCode::NotOpen);
        }

        if(!impl_->system->isOpen() || impl_->system->impl_ == nullptr || impl_->system->impl_->instance != impl_->systemInstance)
        {
            return status(ErrorCode::NotOpen);
        }

        if(impl_->system->impl_->sessionLifetime != systemLifetime)
        {
            return status(ErrorCode::NotOpen);
        }
        return impl_->system->ownedByCurrentThread() ? IO::successStatus() : status(ErrorCode::ResourceBusy);
    }

    IO::Types::Status Source::attach(System &system) noexcept
    {
        if(impl_ == nullptr)
        {
            return status(ErrorCode::OutOfMemory);
        }

        if(impl_->attached)
        {
            const IO::Types::Status attachedStatus = checkAttached();
            if(attachedStatus.ok())
            {
                return impl_->system == &system ? status(ErrorCode::AlreadyOpen) : status(ErrorCode::ResourceBusy);
            }

            if(attachedStatus.code != ErrorCode::NotOpen)
            {
                return attachedStatus;
            }
            impl_->system = nullptr;
            impl_->systemLifetime.reset();
            impl_->systemInstance = 0;
            impl_->attached = false;
            impl_->devices.clear();
        }

        if(!system.isOpen())
        {
            return status(ErrorCode::NotOpen);
        }

        if(!system.ownedByCurrentThread())
        {
            return status(ErrorCode::ResourceBusy);
        }
        impl_->system = &system;
        impl_->systemLifetime = system.impl_->sessionLifetime;
        impl_->systemInstance = system.impl_->instance;
        impl_->attached = true;
        return IO::successStatus();
    }

    IO::Types::Status Source::detach() noexcept
    {
        if(impl_ == nullptr || !impl_->attached)
        {
            return IO::successStatus();
        }

        const IO::Types::Status attachedStatus = checkAttached();
        if(!attachedStatus.ok())
        {
            if(attachedStatus.code == ErrorCode::NotOpen)
            {
                impl_->system = nullptr;
                impl_->systemLifetime.reset();
                impl_->systemInstance = 0;
                impl_->attached = false;
                impl_->devices.clear();
                return IO::successStatus();
            }
            return attachedStatus;
        }

        while(!impl_->devices.empty())
        {
            const IO::Types::Status removeStatus = removeDevice(impl_->devices.back());
            if(!removeStatus.ok())
            {
                return removeStatus;
            }
        }

        impl_->system = nullptr;
        impl_->systemLifetime.reset();
        impl_->systemInstance = 0;
        impl_->attached = false;
        return IO::successStatus();
    }

    // ------------------------------------------------------------
    // Synthetic device lifecycle
    // ------------------------------------------------------------
    IO::Types::Status Source::addDevice(const Types::SyntheticDeviceDescription &description, Types::DeviceId &outDevice) noexcept
    {
        outDevice = {};

        if(impl_ == nullptr)
        {
            return status(ErrorCode::OutOfMemory);
        }

        const IO::Types::Status attachedStatus = checkAttached();
        if(!attachedStatus.ok())
        {
            return attachedStatus;
        }

        if(description.name.empty() || description.persistentKey.empty() || description.elements.empty())
        {
            return status(ErrorCode::InvalidArgument);
        }

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
                return status(ErrorCode::InvalidArgument);
            }

            if(!descriptionElement.components.empty())
            {
                const std::size_t expectedCount = componentCount(descriptionElement.kind);
                if(descriptionElement.components.size() != expectedCount)
                {
                    return status(ErrorCode::InvalidArgument);
                }

                for(std::size_t index = 0; index < descriptionElement.components.size(); ++index)
                {
                    if(descriptionElement.components[index].index != index)
                    {
                        return status(ErrorCode::InvalidArgument);
                    }
                }
            }

            for(const Types::SyntheticElementDescription &other : description.elements)
            {
                if(&descriptionElement != &other && descriptionElement.persistentKey == other.persistentKey)
                {
                    return status(ErrorCode::InvalidArgument);
                }
            }
        }

        System::Impl &state = *impl_->system->impl_;
        const std::size_t initialDeviceCount = state.devices.size();
        const std::size_t initialStateCount = state.states.size();
        const std::size_t initialSourceDeviceCount = impl_->devices.size();
        const std::size_t initialEventCount = state.pendingEvents.size();
        const std::uint32_t initialDeviceSlot = state.nextDeviceSlot;
        const std::uint32_t initialElementSlot = state.nextElementSlot;
        const std::uint64_t initialDeviceRevision = state.deviceRevision;
        const std::uint64_t initialDroppedEvents = state.pendingDroppedEvents;
        const bool initialDiscontinuity = state.pendingDiscontinuity;
        const Types::Health initialHealth = state.health;

        const auto rollback = [&] noexcept
        {
            state.devices.resize(initialDeviceCount);
            state.states.resize(initialStateCount);
            impl_->devices.resize(initialSourceDeviceCount);
            state.pendingEvents.resize(initialEventCount);
            state.nextDeviceSlot = initialDeviceSlot;
            state.nextElementSlot = initialElementSlot;
            state.deviceRevision = initialDeviceRevision;
            state.pendingDroppedEvents = initialDroppedEvents;
            state.pendingDiscontinuity = initialDiscontinuity;
            state.health = initialHealth;
        };

        try
        {
            state.devices.reserve(state.devices.size() + 1);
            state.states.reserve(state.states.size() + description.elements.size());
            impl_->devices.reserve(impl_->devices.size() + 1);

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
                    const std::size_t count = componentCount(element.kind);
                    element.components.reserve(count);
                    for(std::size_t index = 0; index < count; ++index)
                    {
                        element.components.push_back(defaultComponent(index, element.name));
                    }
                }

                device.elements.push_back(std::move(element));
                newStates.push_back({
                    .id = elementId,
                    .value = Internal::zeroValue(descriptionElement.kind),
                    .validity = Types::ValueValidity::Unknown,
                });
            }

            state.devices.push_back(std::move(device));
            for(Types::ElementState &newState : newStates)
            {
                state.states.push_back(newState);
            }

            impl_->devices.push_back(deviceId);
            ++state.health.activeDeviceCount;

            state.appendEvent({
                .timestamp = timestampOrNow({}),
                .kind = Types::EventKind::DeviceConnected,
                .cause = Types::EventCause::Synthesized,
                .data = Types::DeviceEvent{deviceId, state.deviceRevision},
            });

            outDevice = deviceId;
            return IO::successStatus();
        }
        catch(const std::bad_alloc &)
        {
            rollback();
            return status(ErrorCode::OutOfMemory);
        }
    }

    IO::Types::Status Source::removeDevice(Types::DeviceId device) noexcept
    {
        if(impl_ == nullptr)
        {
            return status(ErrorCode::OutOfMemory);
        }

        const IO::Types::Status attachedStatus = checkAttached();
        if(!attachedStatus.ok())
        {
            return attachedStatus;
        }

        if(!ownsDevice(*impl_, device))
        {
            return status(ErrorCode::NotFound);
        }
        System::Impl &state = *impl_->system->impl_;
        const std::size_t deviceIndex = state.findDeviceIndex(device);
        if(deviceIndex == state.devices.size())
        {
            return status(ErrorCode::NotFound);
        }

        try
        {
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

            const std::uint64_t revision = ++state.deviceRevision;
            state.devices.erase(state.devices.begin() + static_cast<std::ptrdiff_t>(deviceIndex));

            if(state.health.activeDeviceCount != 0)
            {
                --state.health.activeDeviceCount;
            }

            state.appendEvent({
                .timestamp = timestampOrNow({}),
                .kind = Types::EventKind::DeviceDisconnected,
                .cause = Types::EventCause::Disconnected,
                .data = Types::DeviceEvent{device, revision},
            });

            const auto owned = std::find(impl_->devices.begin(), impl_->devices.end(), device);
            impl_->devices.erase(owned);
            return IO::successStatus();
        }
        catch(const std::bad_alloc &)
        {
            return status(ErrorCode::OutOfMemory);
        }
    }

    // ------------------------------------------------------------
    // Synthetic state and event publication
    // ------------------------------------------------------------
    IO::Types::Status Source::setValue(Types::ElementId element, const Types::Value &value, Types::Timestamp when) noexcept
    {
        return publishValue(element, value, std::nullopt, when);
    }

    IO::Types::Status Source::setAbsoluteValue(Types::ElementId element, const Types::Value &value, Types::Timestamp when) noexcept
    {
        return publishValue(element, value, Types::ReportingMode::Absolute, when);
    }

    IO::Types::Status Source::addRelativeDelta(Types::ElementId element, const Types::Value &delta, Types::Timestamp when) noexcept
    {
        return publishValue(element, delta, Types::ReportingMode::Relative, when);
    }

    IO::Types::Status Source::publishValue(
        Types::ElementId element,
        const Types::Value &value,
        std::optional<Types::ReportingMode> expectedReporting,
        Types::Timestamp when) noexcept
    {
        if(impl_ == nullptr)
        {
            return status(ErrorCode::OutOfMemory);
        }

        const IO::Types::Status attachedStatus = checkAttached();
        if(!attachedStatus.ok())
        {
            return attachedStatus;
        }

        if(!ownsDevice(*impl_, element.device))
        {
            return status(ErrorCode::NotFound);
        }
        System::Impl &state = *impl_->system->impl_;
        const Types::ElementInfo *info = state.findElement(element);
        const std::size_t stateIndex = state.findStateIndex(element);
        if(info == nullptr || stateIndex == state.states.size())
        {
            return status(ErrorCode::NotFound);
        }

        if(expectedReporting.has_value() && info->reporting != *expectedReporting)
        {
            return status(ErrorCode::InvalidArgument);
        }

        if(!Internal::hasValueKind(value, info->kind))
        {
            return status(ErrorCode::InvalidArgument);
        }

        try
        {
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
                        return status(ErrorCode::InvalidArgument);
                    }
                }
                if(!previousKnown)
                {
                    nextValue = value;
                }
            }

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
            return IO::successStatus();
        }
        catch(const std::bad_alloc &)
        {
            return status(ErrorCode::OutOfMemory);
        }
    }

    IO::Types::Status Source::repeat(Types::ElementId element, Types::Timestamp when) noexcept
    {
        if(impl_ == nullptr)
        {
            return status(ErrorCode::OutOfMemory);
        }

        const IO::Types::Status attachedStatus = checkAttached();
        if(!attachedStatus.ok())
        {
            return attachedStatus;
        }

        if(!ownsDevice(*impl_, element.device))
        {
            return status(ErrorCode::NotFound);
        }
        System::Impl &state = *impl_->system->impl_;
        if(state.findElement(element) == nullptr)
        {
            return status(ErrorCode::NotFound);
        }
        const std::size_t stateIndex = state.findStateIndex(element);
        if(stateIndex == state.states.size() || state.states[stateIndex].validity != Types::ValueValidity::Valid)
        {
            return status(ErrorCode::InvalidArgument);
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
        return IO::successStatus();
    }

    IO::Types::Status Source::invalidate(Types::ElementId element, Types::Timestamp when) noexcept
    {
        if(impl_ == nullptr)
        {
            return status(ErrorCode::OutOfMemory);
        }

        const IO::Types::Status attachedStatus = checkAttached();
        if(!attachedStatus.ok())
        {
            return attachedStatus;
        }

        if(!ownsDevice(*impl_, element.device))
        {
            return status(ErrorCode::NotFound);
        }
        System::Impl &state = *impl_->system->impl_;
        const std::size_t stateIndex = state.findStateIndex(element);
        if(state.findElement(element) == nullptr || stateIndex == state.states.size())
        {
            return status(ErrorCode::NotFound);
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
        return IO::successStatus();
    }

    // ------------------------------------------------------------
    // Synthetic text publication
    // ------------------------------------------------------------
    IO::Types::Status Source::commitText(Types::SurfaceId surface, std::string_view utf8, Types::Timestamp when) noexcept
    {
        if(impl_ == nullptr)
        {
            return status(ErrorCode::OutOfMemory);
        }

        const IO::Types::Status attachedStatus = checkAttached();
        if(!attachedStatus.ok())
        {
            return attachedStatus;
        }

        if(!impl_->system->impl_->textEnabled)
        {
            return status(ErrorCode::Unsupported);
        }

        if(surface.value == 0)
        {
            return status(ErrorCode::InvalidArgument);
        }

        if(Unicode::Utf8::validate(utf8).outcome != Unicode::Types::ValidationOutcome::Valid)
        {
            return status(ErrorCode::EncodingFailed);
        }

        System::Impl &state = *impl_->system->impl_;
        if(state.pendingTextUtf8.size() > state.configuration.maximumPendingTextBytes ||
           utf8.size() > state.configuration.maximumPendingTextBytes - state.pendingTextUtf8.size())
        {
            state.pendingDiscontinuity = true;
            return status(ErrorCode::SizeLimitExceeded);
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
            return IO::successStatus();
        }
        catch(const std::bad_alloc &)
        {
            return status(ErrorCode::OutOfMemory);
        }
    }
} // namespace GameWIP::Input::Synthetic
