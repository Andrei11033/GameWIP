/// @file system.cpp
/// @brief Implements the portable owner-thread-affine Input system.

#include "input/internal/system_state.h"
#include "input/internal/results.h"
#include "input/internal/value.h"

#include <atomic>
#include <new>
#include <utility>

namespace GameWIP::Input
{
    namespace
    {
        std::atomic<std::uint64_t> nextSystemInstance{1};

        using StatusCode = GameWIP::Status::Types::Code;
        using OperationStatus = GameWIP::Status::Types::Status;

        [[nodiscard]] OperationStatus status(StatusCode code) noexcept
        {
            return OperationStatus(code);
        }

        template <typename State> [[nodiscard]] OperationStatus requireOwner(const State &state) noexcept
        {
            if(!state.open)
            {
                return {};
            }
            return state.ownerThread == std::this_thread::get_id() ? OperationStatus{} : status(StatusCode::ResourceBusy);
        }

        template <typename State> [[nodiscard]] OperationStatus requireOpenOwner(const State &state) noexcept
        {
            if(!state.open)
            {
                return status(StatusCode::NotOpen);
            }
            return state.ownerThread == std::this_thread::get_id() ? OperationStatus{} : status(StatusCode::ResourceBusy);
        }

    } // namespace

    /// ------------------------------------------------------------
    /// Event queue and descriptor lookup
    /// ------------------------------------------------------------

    void Internal::SystemState::appendEvent(Types::Event event) noexcept
    {
        if(pendingEvents.size() >= configuration.eventCapacity)
        {
            ++pendingDroppedEvents;
            ++health.totalDroppedEvents;
            pendingDiscontinuity = true;
            return;
        }

        if(event.timestamp.receivedAt == Types::Clock::time_point{})
        {
            event.timestamp.receivedAt = Types::Clock::now();
        }

        event.sequence = nextSequence++;

        try
        {
            pendingEvents.push_back(std::move(event));
        }
        catch(const std::bad_alloc &)
        {
            ++pendingDroppedEvents;
            ++health.totalDroppedEvents;
            pendingDiscontinuity = true;
        }
    }

    std::size_t Internal::SystemState::findDeviceIndex(Types::DeviceId id) const noexcept
    {
        for(std::size_t index = 0; index < devices.size(); ++index)
        {
            if(devices[index].id == id)
            {
                return index;
            }
        }

        return devices.size();
    }

    std::pair<std::size_t, std::size_t> Internal::SystemState::findElementIndex(Types::ElementId id) const noexcept
    {
        const std::size_t deviceIndex = findDeviceIndex(id.device);
        if(deviceIndex == devices.size())
        {
            return {devices.size(), 0};
        }
        const auto &elements = devices[deviceIndex].elements;

        for(std::size_t elementIndex = 0; elementIndex < elements.size(); ++elementIndex)
        {
            if(elements[elementIndex].id == id)
            {
                return {deviceIndex, elementIndex};
            }
        }

        return {devices.size(), elements.size()};
    }

    std::size_t Internal::SystemState::findStateIndex(Types::ElementId id) const noexcept
    {
        if(!id.isValid() || id.device.instance != instance || id.slot == 0)
        {
            return states.size();
        }

        const std::size_t index = static_cast<std::size_t>(id.slot - 1);
        return index < states.size() && states[index].id == id ? index : states.size();
    }

    Types::ElementInfo *Internal::SystemState::findElement(Types::ElementId id) noexcept
    {
        const auto [deviceIndex, elementIndex] = findElementIndex(id);
        return deviceIndex == devices.size() ? nullptr : &devices[deviceIndex].elements[elementIndex];
    }

    const Types::ElementInfo *Internal::SystemState::findElement(Types::ElementId id) const noexcept
    {
        const auto [deviceIndex, elementIndex] = findElementIndex(id);
        return deviceIndex == devices.size() ? nullptr : &devices[deviceIndex].elements[elementIndex];
    }

    /// ------------------------------------------------------------
    /// System lifecycle and frame capture
    /// ------------------------------------------------------------

    System::System() noexcept
        : impl_(std::unique_ptr<Internal::SystemState>(new (std::nothrow) Internal::SystemState{}))
    {
    }

    System::~System() noexcept
    {
        if(impl_ != nullptr)
        {
            (void)close();
        }
    }

    GameWIP::Status::Types::Result<void> System::open(const Types::Configuration &configuration) noexcept
    {
        if(impl_ == nullptr)
        {
            return Internal::result(status(StatusCode::OutOfMemory));
        }

        if(impl_->open)
        {
            return Internal::result(impl_->ownerThread == std::this_thread::get_id() ? status(StatusCode::AlreadyOpen)
                                                                                     : status(StatusCode::ResourceBusy));
        }

        // Validate queue capacities before allocating session state.
        if(configuration.eventCapacity == 0 || configuration.maximumPendingTextBytes == 0 ||
           (configuration.captureRawReports && (configuration.rawReportCapacity == 0 || configuration.maximumRawReportBytes == 0)))
        {
            return Internal::result(status(StatusCode::InvalidArgument));
        }

        try
        {
            // Establish configuration, ownership, and instance identity.
            impl_->sessionLifetime = std::make_shared<std::byte>();
            impl_->configuration = configuration;
            impl_->ownerThread = std::this_thread::get_id();
            impl_->instance = nextSystemInstance.fetch_add(1, std::memory_order_relaxed);

            if(impl_->instance == 0)
            {
                impl_->instance = nextSystemInstance.fetch_add(1, std::memory_order_relaxed);
            }

            // Reset descriptor, event, and hook state.
            impl_->nextDeviceSlot = 1;
            impl_->nextElementSlot = 1;
            impl_->nextSequence = 1;
            impl_->deviceRevision = 0;
            impl_->textEnabled = configuration.enableText;
            impl_->nativeBeforeCapture = nullptr;
            impl_->nativeClose = nullptr;
            impl_->nativeContext = nullptr;
            impl_->devices.clear();
            impl_->states.clear();
            impl_->pendingEvents.clear();
            impl_->pendingRawReports.clear();
            impl_->pendingTextUtf8.clear();

            // Reserve configured queue capacities before opening the session.
            impl_->pendingEvents.reserve(configuration.eventCapacity);

            if(configuration.captureRawReports)
            {
                impl_->pendingRawReports.reserve(configuration.rawReportCapacity);
            }

            if(configuration.enableText)
            {
                impl_->pendingTextUtf8.reserve(configuration.maximumPendingTextBytes);
            }

            // Publish the initialized session.
            impl_->pendingDroppedEvents = 0;
            impl_->pendingDiscontinuity = false;
            impl_->health = {};
            impl_->open = true;
            return GameWIP::Status::Types::Result<void>::success();
        }
        catch(const std::bad_alloc &)
        {
            impl_->open = false;
            return Internal::result(status(StatusCode::OutOfMemory));
        }
    }

    GameWIP::Status::Types::Result<void> System::close() noexcept
    {
        if(impl_ == nullptr)
        {
            return Internal::result(status(StatusCode::OutOfMemory));
        }

        if(!impl_->open)
        {
            return GameWIP::Status::Types::Result<void>::success();
        }

        const OperationStatus ownerStatus = requireOwner(*impl_);
        if(!ownerStatus.ok())
        {
            return Internal::result(ownerStatus);
        }

        if(impl_->nativeClose != nullptr)
        {
            impl_->nativeClose(*impl_, impl_->nativeContext);
        }

        impl_->nativeBeforeCapture = nullptr;
        impl_->nativeClose = nullptr;
        impl_->nativeContext = nullptr;
        impl_->sessionLifetime.reset();
        impl_->open = false;
        impl_->devices.clear();
        impl_->states.clear();
        impl_->pendingEvents.clear();
        impl_->pendingRawReports.clear();
        impl_->pendingTextUtf8.clear();
        impl_->health.activeDeviceCount = 0;
        return GameWIP::Status::Types::Result<void>::success();
    }

    bool System::isOpen() const noexcept
    {
        return impl_ != nullptr && impl_->open;
    }

    bool System::ownedByCurrentThread() const noexcept
    {
        return impl_ != nullptr && impl_->open && impl_->ownerThread == std::this_thread::get_id();
    }

    Types::CaptureResult System::capture(Types::Frame &destination) noexcept
    {
        if(impl_ == nullptr)
        {
            return Internal::failedResult<Types::CaptureInfo>(status(StatusCode::OutOfMemory));
        }

        const OperationStatus ownerStatus = requireOpenOwner(*impl_);
        if(!ownerStatus.ok())
        {
            return Internal::failedResult<Types::CaptureInfo>(ownerStatus);
        }

        // Collect native input before publishing this frame.
        if(impl_->nativeBeforeCapture != nullptr)
        {
            impl_->nativeBeforeCapture(*impl_, impl_->nativeContext);
        }

        try
        {
            // Reserve frame and queue storage before moving interval data.
            impl_->pendingEvents.reserve(impl_->configuration.eventCapacity);
            destination.events_.reserve(impl_->configuration.eventCapacity);

            if(impl_->configuration.captureRawReports)
            {
                impl_->pendingRawReports.reserve(impl_->configuration.rawReportCapacity);
                destination.rawReports_.reserve(impl_->configuration.rawReportCapacity);
            }

            if(impl_->textEnabled)
            {
                impl_->pendingTextUtf8.reserve(impl_->configuration.maximumPendingTextBytes);
                destination.textUtf8_.reserve(impl_->configuration.maximumPendingTextBytes);
            }

            // Build frame metadata.
            destination.states_.reserve(impl_->states.size());
            destination.states_ = impl_->states;
            destination.sequence_ = impl_->nextSequence++;
            destination.deviceRevision_ = impl_->deviceRevision;
            destination.capturedAt_ = Types::Clock::now();
            destination.discontinuity_ = impl_->pendingDiscontinuity;
            destination.droppedEvents_ = impl_->pendingDroppedEvents;

            // Transfer this interval's events and reports.
            destination.events_.swap(impl_->pendingEvents);
            destination.rawReports_.swap(impl_->pendingRawReports);
            destination.textUtf8_.swap(impl_->pendingTextUtf8);

            impl_->pendingEvents.clear();
            impl_->pendingRawReports.clear();
            impl_->pendingTextUtf8.clear();

            // Reset transition counters and relative values for the next frame.
            for(Types::ElementState &state : impl_->states)
            {
                const Types::ElementInfo *info = impl_->findElement(state.id);

                if(info != nullptr && info->reporting == Types::ReportingMode::Relative)
                {
                    state.value = Internal::zeroValue(info->kind);
                }

                state.becameTrueCount = 0;
                state.becameFalseCount = 0;
                state.repeatCount = 0;
            }

            // Clear loss state only after the frame has been published.
            impl_->pendingDroppedEvents = 0;
            impl_->pendingDiscontinuity = false;

            // Return metadata for the published frame.
            return Types::CaptureResult::success({
                .captureSequence = destination.sequence(),
                .droppedEvents = destination.droppedEvents(),
                .discontinuity = destination.discontinuity(),
            });
        }
        catch(const std::bad_alloc &)
        {
            return Internal::failedResult<Types::CaptureInfo>(status(StatusCode::OutOfMemory));
        }
    }

    /// ------------------------------------------------------------
    /// Descriptor and health queries
    /// ------------------------------------------------------------

    GameWIP::Status::Types::Result<std::span<const Types::DeviceInfo>> System::devices() const noexcept
    {
        if(impl_ == nullptr)
        {
            return Internal::failedResult<std::span<const Types::DeviceInfo>>(status(StatusCode::OutOfMemory));
        }

        const OperationStatus ownerStatus = requireOpenOwner(*impl_);
        if(!ownerStatus.ok())
        {
            return Internal::failedResult<std::span<const Types::DeviceInfo>>(ownerStatus);
        }

        return GameWIP::Status::Types::Result<std::span<const Types::DeviceInfo>>::success(impl_->devices);
    }

    GameWIP::Status::Types::Result<const Types::DeviceInfo *> System::findDevice(Types::DeviceId id) const noexcept
    {
        if(impl_ == nullptr)
        {
            return Internal::failedResult<const Types::DeviceInfo *>(status(StatusCode::OutOfMemory));
        }

        const OperationStatus ownerStatus = requireOpenOwner(*impl_);
        if(!ownerStatus.ok())
        {
            return Internal::failedResult<const Types::DeviceInfo *>(ownerStatus);
        }

        if(!id.isValid())
        {
            return Internal::failedResult<const Types::DeviceInfo *>(status(StatusCode::InvalidArgument));
        }

        const std::size_t index = impl_->findDeviceIndex(id);
        if(index == impl_->devices.size())
        {
            return Internal::failedResult<const Types::DeviceInfo *>(status(StatusCode::NotFound));
        }

        return GameWIP::Status::Types::Result<const Types::DeviceInfo *>::success(&impl_->devices[index]);
    }

    GameWIP::Status::Types::Result<const Types::ElementInfo *> System::findElement(Types::ElementId id) const noexcept
    {
        if(impl_ == nullptr)
        {
            return Internal::failedResult<const Types::ElementInfo *>(status(StatusCode::OutOfMemory));
        }

        const OperationStatus ownerStatus = requireOpenOwner(*impl_);
        if(!ownerStatus.ok())
        {
            return Internal::failedResult<const Types::ElementInfo *>(ownerStatus);
        }

        if(!id.isValid())
        {
            return Internal::failedResult<const Types::ElementInfo *>(status(StatusCode::InvalidArgument));
        }

        const Types::ElementInfo *info = impl_->findElement(id);
        if(info == nullptr)
        {
            return Internal::failedResult<const Types::ElementInfo *>(status(StatusCode::NotFound));
        }
        return GameWIP::Status::Types::Result<const Types::ElementInfo *>::success(info);
    }

    GameWIP::Status::Types::Result<Types::Health> System::health() const noexcept
    {
        if(impl_ == nullptr)
        {
            return Internal::failedResult<Types::Health>(status(StatusCode::OutOfMemory));
        }

        const OperationStatus ownerStatus = requireOpenOwner(*impl_);
        if(!ownerStatus.ok())
        {
            return Internal::failedResult<Types::Health>(ownerStatus);
        }

        return GameWIP::Status::Types::Result<Types::Health>::success(impl_->health);
    }

    GameWIP::Status::Types::Result<Types::DeviceInfo> System::copyDeviceInfo(Types::DeviceId id) const noexcept
    {
        const auto sourceResult = findDevice(id);
        if(!sourceResult.ok())
        {
            return Internal::failedResult<Types::DeviceInfo>(sourceResult.status());
        }

        try
        {
            return GameWIP::Status::Types::Result<Types::DeviceInfo>::success(*sourceResult.value());
        }
        catch(const std::bad_alloc &)
        {
            return Internal::failedResult<Types::DeviceInfo>(status(StatusCode::OutOfMemory));
        }
    }

    /// ------------------------------------------------------------
    /// Text capture configuration
    /// ------------------------------------------------------------

    GameWIP::Status::Types::Result<void> System::setTextEnabled(bool enabled) noexcept
    {
        if(impl_ == nullptr)
        {
            return Internal::result(status(StatusCode::OutOfMemory));
        }

        const OperationStatus ownerStatus = requireOpenOwner(*impl_);
        if(!ownerStatus.ok())
        {
            return Internal::result(ownerStatus);
        }

        if(!enabled)
        {
            impl_->textEnabled = false;
            return GameWIP::Status::Types::Result<void>::success();
        }

        try
        {
            impl_->pendingTextUtf8.reserve(impl_->configuration.maximumPendingTextBytes);
            impl_->textEnabled = true;
            return GameWIP::Status::Types::Result<void>::success();
        }
        catch(const std::bad_alloc &)
        {
            return Internal::result(status(StatusCode::OutOfMemory));
        }
    }

    GameWIP::Status::Types::Result<void> System::setMaximumPendingTextBytes(std::size_t maximumBytes) noexcept
    {
        if(impl_ == nullptr)
        {
            return Internal::result(status(StatusCode::OutOfMemory));
        }

        const OperationStatus ownerStatus = requireOpenOwner(*impl_);
        if(!ownerStatus.ok())
        {
            return Internal::result(ownerStatus);
        }

        if(maximumBytes == 0)
        {
            return Internal::result(status(StatusCode::InvalidArgument));
        }

        if(impl_->pendingTextUtf8.size() > maximumBytes)
        {
            return Internal::result(status(StatusCode::SizeLimitExceeded));
        }

        try
        {
            if(impl_->textEnabled)
            {
                impl_->pendingTextUtf8.reserve(maximumBytes);
            }
            impl_->configuration.maximumPendingTextBytes = maximumBytes;
            return GameWIP::Status::Types::Result<void>::success();
        }
        catch(const std::bad_alloc &)
        {
            return Internal::result(status(StatusCode::OutOfMemory));
        }
    }
} // namespace GameWIP::Input
