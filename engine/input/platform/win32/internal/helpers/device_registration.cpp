/// @file device_registration.cpp
/// @brief Win32 device-registration rollback state.

#include "input/platform/win32/internal/device_registration.h"

namespace GameWIP::Input::Platform::Win32::Detail
{
    /// ------------------------------------------------------------
    /// Registration snapshot and rollback
    /// ------------------------------------------------------------

    DeviceRegistrationRollback::DeviceRegistrationRollback(System::Impl &state, Attachment::Impl &attachment) noexcept
        : state_(state)
        , attachment_(attachment)
    {
        // Record collection sizes.
        initialDeviceCount_ = state_.devices.size();
        initialStateCount_ = state_.states.size();
        initialBindingCount_ = attachment_.devices.size();
        initialEventCount_ = state_.pendingEvents.size();

        // Record counters and session health.
        initialDeviceSlot_ = state_.nextDeviceSlot;
        initialElementSlot_ = state_.nextElementSlot;
        initialDeviceRevision_ = state_.deviceRevision;
        initialDroppedEvents_ = state_.pendingDroppedEvents;
        initialDiscontinuity_ = state_.pendingDiscontinuity;
        initialHealth_ = state_.health;
    }

    void DeviceRegistrationRollback::saveMergedDevice(const Types::DeviceInfo &descriptor)
    {
        // Preserve descriptor fields that merging this source can update.
        initialMergedName_ = descriptor.name;
        initialMergedManufacturer_ = descriptor.manufacturer;

        hasMergedDevice_ = true;
        mergedDeviceId_ = descriptor.id;

        initialMergedSourceCount_ = descriptor.sources.size();
        initialMergedElementCount_ = descriptor.elements.size();

        initialMergedConnected_ = descriptor.connected;
        initialMergedRevision_ = descriptor.descriptorRevision;
        initialMergedConfidence_ = descriptor.identityConfidence;

        initialMergedVendorId_ = descriptor.vendorId;
        initialMergedProductId_ = descriptor.productId;
    }

    void DeviceRegistrationRollback::restore() noexcept
    {
        // Remove new descriptors before restoring a merged one.
        state_.devices.resize(initialDeviceCount_);
        if(hasMergedDevice_)
        {
            const std::size_t deviceIndex = state_.findDeviceIndex(mergedDeviceId_);
            if(deviceIndex != state_.devices.size())
            {
                Types::DeviceInfo &descriptor = state_.devices[deviceIndex];
                descriptor.sources.resize(initialMergedSourceCount_);
                descriptor.elements.resize(initialMergedElementCount_);

                descriptor.connected = initialMergedConnected_;
                descriptor.descriptorRevision = initialMergedRevision_;
                descriptor.identityConfidence = initialMergedConfidence_;

                descriptor.vendorId = initialMergedVendorId_;
                descriptor.productId = initialMergedProductId_;

                descriptor.name.swap(initialMergedName_);
                descriptor.manufacturer.swap(initialMergedManufacturer_);
            }
        }

        // Restore event, binding, and state collections.
        state_.states.resize(initialStateCount_);
        attachment_.devices.resize(initialBindingCount_);
        state_.pendingEvents.resize(initialEventCount_);

        // Restore counters and health changed during registration.
        state_.nextDeviceSlot = initialDeviceSlot_;
        state_.nextElementSlot = initialElementSlot_;
        state_.deviceRevision = initialDeviceRevision_;
        state_.pendingDroppedEvents = initialDroppedEvents_;
        state_.pendingDiscontinuity = initialDiscontinuity_;
        state_.health = initialHealth_;
    }
} // namespace GameWIP::Input::Platform::Win32::Detail
