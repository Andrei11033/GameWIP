/// @file device_registration.h
/// @brief Rollback state for an incomplete Win32 device registration.

#pragma once

#include "input/platform/win32/internal/win32_attachment_state.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace GameWIP::Input::Platform::Win32
{
    /// @brief Restores session state when native device registration fails.
    class DeviceRegistrationRollback final
    {
    public:
        DeviceRegistrationRollback(System::Impl &state, Attachment::Impl &attachment) noexcept;

        void saveMergedDevice(const Types::DeviceInfo &descriptor);

        void restore() noexcept;

    private:
        System::Impl &state_;
        Attachment::Impl &attachment_;

        // Session collection sizes
        std::size_t initialDeviceCount_ = 0;
        std::size_t initialStateCount_ = 0;
        std::size_t initialBindingCount_ = 0;
        std::size_t initialEventCount_ = 0;

        // Session counters
        std::uint32_t initialDeviceSlot_ = 0;
        std::uint32_t initialElementSlot_ = 0;
        std::uint64_t initialDeviceRevision_ = 0;
        std::uint64_t initialDroppedEvents_ = 0;
        bool initialDiscontinuity_ = false;
        Types::Health initialHealth_{};

        // Merged descriptor identity and names
        bool hasMergedDevice_ = false;
        Types::DeviceId mergedDeviceId_{};
        std::string initialMergedName_;
        std::string initialMergedManufacturer_;

        // Merged descriptor collections and state
        std::size_t initialMergedSourceCount_ = 0;
        std::size_t initialMergedElementCount_ = 0;
        bool initialMergedConnected_ = false;
        std::uint64_t initialMergedRevision_ = 0;
        Types::IdentityConfidence initialMergedConfidence_ = Types::IdentityConfidence::None;
        std::optional<std::uint16_t> initialMergedVendorId_{};
        std::optional<std::uint16_t> initialMergedProductId_{};
    };
} // namespace GameWIP::Input::Platform::Win32
