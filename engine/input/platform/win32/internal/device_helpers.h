/// @file device_helpers.h
/// @brief Shared private helpers for Win32 device registration.

#pragma once

#include "input/platform/win32/internal/win32_attachment_state.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

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
        std::uint64_t revision) noexcept;
    [[nodiscard]] Types::DeviceInfo *findLogicalDevice(System::Impl &state, std::string_view persistentKey) noexcept;
    void publishDeviceRegistration(
        System::Impl &state,
        const Types::DeviceInfo &descriptor,
        bool newLogicalDevice,
        bool logicalWasConnected,
        bool connected) noexcept;

    /// ------------------------------------------------------------
    /// Element descriptor helpers
    /// ------------------------------------------------------------

    [[nodiscard]] std::string standardKey(Types::StandardId standard);
    [[nodiscard]] Types::NumericInfo numericInfo(
        Types::NumericStorage storage,
        std::optional<Types::Scalar> minimum,
        std::optional<Types::Scalar> maximum,
        Types::Unit unit) noexcept;
    [[nodiscard]] Types::ComponentInfo component(std::uint8_t index, std::string_view name, Types::NumericInfo numeric);
} // namespace GameWIP::Input::Platform::Win32::Detail
