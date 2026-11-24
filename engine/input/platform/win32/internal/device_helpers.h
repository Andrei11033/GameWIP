/// @file device_helpers.h
/// @brief Shared private helpers for Win32 device registration.

#pragma once

#include "input/platform/win32/internal/win32_attachment_state.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace GameWIP::Input::Platform::Win32
{
    /// ------------------------------------------------------------
    /// Status, clock, and window helpers
    /// ------------------------------------------------------------

    [[nodiscard]] IO::Types::Status status(ErrorCode code) noexcept;
    [[nodiscard]] IO::Types::Status nativeStatus(std::uint32_t code) noexcept;
    [[nodiscard]] Types::Timestamp now() noexcept;
    [[nodiscard]] std::uint64_t surfaceValue(const void *windowHandle) noexcept;

    /// ------------------------------------------------------------
    /// Device and element lookup
    /// ------------------------------------------------------------

    [[nodiscard]] Types::ElementState *findState(SystemState &state, Types::ElementId id) noexcept;
    [[nodiscard]] Types::ElementInfo *findElement(SystemState &state, Types::ElementId id) noexcept;
    [[nodiscard]] DeviceBinding *findDevice(AttachmentState &attachment, DeviceRole role, std::uint32_t slot) noexcept;
    [[nodiscard]] DeviceBinding *findNativeDevice(AttachmentState &attachment, DeviceRole role, void *nativeDevice) noexcept;
    [[nodiscard]] DeviceBinding *findHidDevice(AttachmentState &attachment, void *nativeDevice) noexcept;
    [[nodiscard]] ElementBinding *findHidElement(DeviceBinding &device, Types::HidControlId hid) noexcept;
    [[nodiscard]] ElementBinding *findRole(DeviceBinding &device, ElementRole role) noexcept;
    [[nodiscard]] ElementBinding *standardElement(DeviceBinding &device, Types::StandardId standard) noexcept;

    /// ------------------------------------------------------------
    /// Attachment validation
    /// ------------------------------------------------------------

    [[nodiscard]] IO::Types::Status checkAttached(const AttachmentState &attachment) noexcept;

    /// ------------------------------------------------------------
    /// Device state and registration
    /// ------------------------------------------------------------

    void appendElementEvent(
        SystemState &state,
        Types::ElementId id,
        Types::Value value,
        Types::Value previous,
        bool previousKnown,
        Types::EventCause cause = Types::EventCause::Reported) noexcept;
    void invalidateDevice(SystemState &state, AttachmentState &attachment, DeviceBinding &device, Types::EventCause cause) noexcept;
    void reconnectDevice(SystemState &state, DeviceBinding &device) noexcept;
    Types::ElementId addElement(
        SystemState &state,
        Types::DeviceInfo &device,
        DeviceBinding &binding,
        std::string name,
        std::string key,
        std::optional<Types::StandardId> standard,
        Types::ValueKind kind,
        Types::ReportingMode reporting,
        std::vector<Types::ComponentInfo> components,
        ElementRole role,
        std::optional<Types::HidControlId> hid = std::nullopt);
    void addDevice(
        SystemState &state,
        AttachmentState &attachment,
        DeviceRole role,
        std::uint32_t slot,
        std::string name,
        std::string manufacturer,
        bool connected,
        Types::SourceInfo source = {},
        std::string_view logicalPersistentKey = {},
        Types::IdentityConfidence logicalIdentityConfidence = Types::IdentityConfidence::None);

    /// ------------------------------------------------------------
    /// Logical device registration
    /// ------------------------------------------------------------

    void appendDeviceEvent(
        SystemState &state,
        Types::EventKind kind,
        Types::EventCause cause,
        Types::DeviceId device,
        std::uint64_t revision) noexcept;
    [[nodiscard]] Types::DeviceInfo *findLogicalDevice(SystemState &state, std::string_view persistentKey) noexcept;
    void publishDeviceRegistration(
        SystemState &state,
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
} // namespace GameWIP::Input::Platform::Win32
