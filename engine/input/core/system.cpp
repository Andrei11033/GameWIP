/// @file system.cpp
/// @brief Implements the portable owner-thread-affine Input system.

#include "input/internal/system_state.h"
#include "input/internal/value.h"

#include <atomic>
#include <new>
#include <utility>

namespace GameWIP::Input
{
    namespace
    {
        std::atomic<std::uint64_t> nextSystemInstance{1};

        [[nodiscard]] IO::Types::Status status(IO::Types::ErrorCode code) noexcept
        {
            return IO::makeStatus(code);
        }

        template <typename State> [[nodiscard]] IO::Types::Status requireOwner(const State &state) noexcept
        {
            if (!state.open)
            {
                return IO::successStatus();
            }
            return state.ownerThread == std::this_thread::get_id() ? IO::successStatus() : status(IO::Types::ErrorCode::ResourceBusy);
        }

        template <typename State> [[nodiscard]] IO::Types::Status requireOpenOwner(const State &state) noexcept
        {
            if (!state.open)
            {
                return status(IO::Types::ErrorCode::NotOpen);
            }
            return state.ownerThread == std::this_thread::get_id() ? IO::successStatus() : status(IO::Types::ErrorCode::ResourceBusy);
        }

    } // namespace

    // ------------------------------------------------------------
    // Event queue and descriptor lookup
    // ------------------------------------------------------------
    void System::Impl::appendEvent(Types::Event event) noexcept
    {
        if (pendingEvents.size() >= configuration.eventCapacity)
        {
            ++pendingDroppedEvents;
            ++health.totalDroppedEvents;
            pendingDiscontinuity = true;
            return;
        }

        if (event.timestamp.receivedAt == Types::Clock::time_point{})
        {
            event.timestamp.receivedAt = Types::Clock::now();
        }
        event.sequence = nextSequence++;

        try
        {
            pendingEvents.push_back(std::move(event));
        }
        catch (const std::bad_alloc &)
        {
            ++pendingDroppedEvents;
            ++health.totalDroppedEvents;
            pendingDiscontinuity = true;
        }
    }

    std::size_t System::Impl::findDeviceIndex(Types::DeviceId id) const noexcept
    {
        for (std::size_t index = 0; index < devices.size(); ++index)
        {
            if (devices[index].id == id)
            {
                return index;
            }
        }
        return devices.size();
    }

    std::pair<std::size_t, std::size_t> System::Impl::findElementIndex(Types::ElementId id) const noexcept
    {
        const std::size_t deviceIndex = findDeviceIndex(id.device);
        if (deviceIndex == devices.size())
        {
            return {devices.size(), 0};
        }

        const auto &elements = devices[deviceIndex].elements;
        for (std::size_t elementIndex = 0; elementIndex < elements.size(); ++elementIndex)
        {
            if (elements[elementIndex].id == id)
            {
                return {deviceIndex, elementIndex};
            }
        }
        return {devices.size(), elements.size()};
    }

    std::size_t System::Impl::findStateIndex(Types::ElementId id) const noexcept
    {
        if (!id.isValid() || id.device.instance != instance || id.slot == 0)
        {
            return states.size();
        }

        const std::size_t index = static_cast<std::size_t>(id.slot - 1);
        return index < states.size() && states[index].id == id ? index : states.size();
    }

    Types::ElementInfo *System::Impl::findElement(Types::ElementId id) noexcept
    {
        const auto [deviceIndex, elementIndex] = findElementIndex(id);
        return deviceIndex == devices.size() ? nullptr : &devices[deviceIndex].elements[elementIndex];
    }

    const Types::ElementInfo *System::Impl::findElement(Types::ElementId id) const noexcept
    {
        const auto [deviceIndex, elementIndex] = findElementIndex(id);
        return deviceIndex == devices.size() ? nullptr : &devices[deviceIndex].elements[elementIndex];
    }

    // ------------------------------------------------------------
    // System lifecycle and frame capture
    // ------------------------------------------------------------
    System::System() noexcept
        : impl_(std::unique_ptr<Impl>(new (std::nothrow) Impl{}))
    {
    }

    System::~System() noexcept
    {
        if (impl_ != nullptr)
        {
            (void)close();
        }
    }

    IO::Types::Status System::open(const Types::Configuration &configuration) noexcept
    {
        if (impl_ == nullptr)
        {
            return status(IO::Types::ErrorCode::OutOfMemory);
        }
        if (impl_->open)
        {
            return impl_->ownerThread == std::this_thread::get_id() ? status(IO::Types::ErrorCode::AlreadyOpen)
                                                                    : status(IO::Types::ErrorCode::ResourceBusy);
        }
        if (configuration.eventCapacity == 0 || configuration.maximumPendingTextBytes == 0 ||
            (configuration.captureRawReports && (configuration.rawReportCapacity == 0 || configuration.maximumRawReportBytes == 0)))
        {
            return status(IO::Types::ErrorCode::InvalidArgument);
        }

        try
        {
            impl_->sessionLifetime = std::make_shared<std::byte>();
            impl_->configuration = configuration;
            impl_->ownerThread = std::this_thread::get_id();
            impl_->instance = nextSystemInstance.fetch_add(1, std::memory_order_relaxed);
            if (impl_->instance == 0)
            {
                impl_->instance = nextSystemInstance.fetch_add(1, std::memory_order_relaxed);
            }
            impl_->nextDeviceSlot = 1;
            impl_->nextElementSlot = 1;
            impl_->nextSequence = 1;
            impl_->deviceRevision = 0;
            impl_->textEnabled = configuration.enableText;
            impl_->nativeBeforeCapture = nullptr;
            impl_->nativeAfterCapture = nullptr;
            impl_->nativeClose = nullptr;
            impl_->nativeContext = nullptr;
            impl_->devices.clear();
            impl_->states.clear();
            impl_->pendingEvents.clear();
            impl_->pendingRawReports.clear();
            impl_->pendingTextUtf8.clear();
            impl_->pendingEvents.reserve(configuration.eventCapacity);
            if (configuration.captureRawReports)
            {
                impl_->pendingRawReports.reserve(configuration.rawReportCapacity);
            }
            if (configuration.enableText)
            {
                impl_->pendingTextUtf8.reserve(configuration.maximumPendingTextBytes);
            }
            impl_->pendingDroppedEvents = 0;
            impl_->pendingDiscontinuity = false;
            impl_->health = {};
            impl_->open = true;
            return IO::successStatus();
        }
        catch (const std::bad_alloc &)
        {
            impl_->open = false;
            return status(IO::Types::ErrorCode::OutOfMemory);
        }
    }

    IO::Types::Status System::close() noexcept
    {
        if (impl_ == nullptr)
        {
            return status(IO::Types::ErrorCode::OutOfMemory);
        }
        if (!impl_->open)
        {
            return IO::successStatus();
        }
        const IO::Types::Status ownerStatus = requireOwner(*impl_);
        if (!ownerStatus.ok())
        {
            return ownerStatus;
        }

        if (impl_->nativeClose != nullptr)
        {
            impl_->nativeClose(*impl_, impl_->nativeContext);
        }
        impl_->nativeBeforeCapture = nullptr;
        impl_->nativeAfterCapture = nullptr;
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
        return IO::successStatus();
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
        Types::CaptureResult result{};
        if (impl_ == nullptr)
        {
            result.status = status(IO::Types::ErrorCode::OutOfMemory);
            return result;
        }

        const IO::Types::Status ownerStatus = requireOpenOwner(*impl_);
        if (!ownerStatus.ok())
        {
            result.status = ownerStatus;
            return result;
        }

        if (impl_->nativeBeforeCapture != nullptr)
        {
            impl_->nativeBeforeCapture(*impl_, impl_->nativeContext);
        }

        try
        {
            impl_->pendingEvents.reserve(impl_->configuration.eventCapacity);
            destination.events_.reserve(impl_->configuration.eventCapacity);
            if (impl_->configuration.captureRawReports)
            {
                impl_->pendingRawReports.reserve(impl_->configuration.rawReportCapacity);
                destination.rawReports_.reserve(impl_->configuration.rawReportCapacity);
            }
            if (impl_->textEnabled)
            {
                impl_->pendingTextUtf8.reserve(impl_->configuration.maximumPendingTextBytes);
                destination.textUtf8_.reserve(impl_->configuration.maximumPendingTextBytes);
            }
            destination.states_.reserve(impl_->states.size());
            destination.states_ = impl_->states;
            destination.sequence_ = impl_->nextSequence++;
            destination.deviceRevision_ = impl_->deviceRevision;
            destination.capturedAt_ = Types::Clock::now();
            destination.discontinuity_ = impl_->pendingDiscontinuity;
            destination.droppedEvents_ = impl_->pendingDroppedEvents;
            destination.events_.swap(impl_->pendingEvents);
            destination.rawReports_.swap(impl_->pendingRawReports);
            destination.textUtf8_.swap(impl_->pendingTextUtf8);

            impl_->pendingEvents.clear();
            impl_->pendingRawReports.clear();
            impl_->pendingTextUtf8.clear();
            if (impl_->nativeAfterCapture != nullptr)
            {
                impl_->nativeAfterCapture(*impl_, impl_->nativeContext);
            }
            for (Types::ElementState &state : impl_->states)
            {
                const Types::ElementInfo *info = impl_->findElement(state.id);
                if (info != nullptr && info->reporting == Types::ReportingMode::Relative)
                {
                    state.value = Internal::zeroValue(info->kind);
                }
                state.becameTrueCount = 0;
                state.becameFalseCount = 0;
                state.repeatCount = 0;
            }
            impl_->pendingDroppedEvents = 0;
            impl_->pendingDiscontinuity = false;

            result.status = IO::successStatus();
            result.published = true;
            result.captureSequence = destination.sequence();
            result.droppedEvents = destination.droppedEvents();
            result.discontinuity = destination.discontinuity();
            return result;
        }
        catch (const std::bad_alloc &)
        {
            result.status = status(IO::Types::ErrorCode::OutOfMemory);
            return result;
        }
    }

    // ------------------------------------------------------------
    // Descriptor and health queries
    // ------------------------------------------------------------
    IO::Types::Status System::devices(std::span<const Types::DeviceInfo> &outDevices) const noexcept
    {
        outDevices = {};
        if (impl_ == nullptr)
        {
            return status(IO::Types::ErrorCode::OutOfMemory);
        }
        const IO::Types::Status ownerStatus = requireOpenOwner(*impl_);
        if (!ownerStatus.ok())
        {
            return ownerStatus;
        }
        outDevices = impl_->devices;
        return IO::successStatus();
    }

    IO::Types::Status System::findDevice(Types::DeviceId id, const Types::DeviceInfo *&outInfo) const noexcept
    {
        outInfo = nullptr;
        if (impl_ == nullptr)
        {
            return status(IO::Types::ErrorCode::OutOfMemory);
        }
        const IO::Types::Status ownerStatus = requireOpenOwner(*impl_);
        if (!ownerStatus.ok())
        {
            return ownerStatus;
        }
        if (!id.isValid())
        {
            return status(IO::Types::ErrorCode::InvalidArgument);
        }

        const std::size_t index = impl_->findDeviceIndex(id);
        if (index == impl_->devices.size())
        {
            return status(IO::Types::ErrorCode::NotFound);
        }
        outInfo = &impl_->devices[index];
        return IO::successStatus();
    }

    IO::Types::Status System::findElement(Types::ElementId id, const Types::ElementInfo *&outInfo) const noexcept
    {
        outInfo = nullptr;
        if (impl_ == nullptr)
        {
            return status(IO::Types::ErrorCode::OutOfMemory);
        }
        const IO::Types::Status ownerStatus = requireOpenOwner(*impl_);
        if (!ownerStatus.ok())
        {
            return ownerStatus;
        }
        if (!id.isValid())
        {
            return status(IO::Types::ErrorCode::InvalidArgument);
        }

        outInfo = impl_->findElement(id);
        return outInfo == nullptr ? status(IO::Types::ErrorCode::NotFound) : IO::successStatus();
    }

    IO::Types::Status System::health(Types::Health &outHealth) const noexcept
    {
        if (impl_ == nullptr)
        {
            return status(IO::Types::ErrorCode::OutOfMemory);
        }
        const IO::Types::Status ownerStatus = requireOpenOwner(*impl_);
        if (!ownerStatus.ok())
        {
            return ownerStatus;
        }
        outHealth = impl_->health;
        return IO::successStatus();
    }

    IO::Types::Status System::copyDeviceInfo(Types::DeviceId id, Types::DeviceInfo &outInfo) const noexcept
    {
        const Types::DeviceInfo *source = nullptr;
        const IO::Types::Status findStatus = findDevice(id, source);
        if (!findStatus.ok())
        {
            return findStatus;
        }

        try
        {
            outInfo = *source;
            return IO::successStatus();
        }
        catch (const std::bad_alloc &)
        {
            return status(IO::Types::ErrorCode::OutOfMemory);
        }
    }

    // ------------------------------------------------------------
    // Text capture configuration
    // ------------------------------------------------------------
    IO::Types::Status System::setTextEnabled(bool enabled) noexcept
    {
        if (impl_ == nullptr)
        {
            return status(IO::Types::ErrorCode::OutOfMemory);
        }
        const IO::Types::Status ownerStatus = requireOpenOwner(*impl_);
        if (!ownerStatus.ok())
        {
            return ownerStatus;
        }
        if (!enabled)
        {
            impl_->textEnabled = false;
            return IO::successStatus();
        }

        try
        {
            impl_->pendingTextUtf8.reserve(impl_->configuration.maximumPendingTextBytes);
            impl_->textEnabled = true;
            return IO::successStatus();
        }
        catch (const std::bad_alloc &)
        {
            return status(IO::Types::ErrorCode::OutOfMemory);
        }
    }

    IO::Types::Status System::setMaximumPendingTextBytes(std::size_t maximumBytes) noexcept
    {
        if (impl_ == nullptr)
        {
            return status(IO::Types::ErrorCode::OutOfMemory);
        }
        const IO::Types::Status ownerStatus = requireOpenOwner(*impl_);
        if (!ownerStatus.ok())
        {
            return ownerStatus;
        }
        if (maximumBytes == 0)
        {
            return status(IO::Types::ErrorCode::InvalidArgument);
        }
        if (impl_->pendingTextUtf8.size() > maximumBytes)
        {
            return status(IO::Types::ErrorCode::SizeLimitExceeded);
        }
        try
        {
            if (impl_->textEnabled)
            {
                impl_->pendingTextUtf8.reserve(maximumBytes);
            }
            impl_->configuration.maximumPendingTextBytes = maximumBytes;
            return IO::successStatus();
        }
        catch (const std::bad_alloc &)
        {
            return status(IO::Types::ErrorCode::OutOfMemory);
        }
    }
} // namespace GameWIP::Input
