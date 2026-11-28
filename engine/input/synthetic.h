/// @file synthetic.h
/// @brief Optional public virtual-device producer for deterministic tests.
#pragma once

#include "input/system.h"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace GameWIP::Input::Types
{
    /// @brief Description used to create one synthetic element.
    struct SyntheticElementDescription
    {
        std::string name{};                                ///< Human-readable UTF-8 element name.
        std::string persistentKey{};                       ///< Stable binding key within the synthetic source.
        ControlIdentity identity{};                        ///< Optional standard or canonical HID identity.
        ValueKind kind = ValueKind::None;                  ///< Typed value kind.
        ReportingMode reporting = ReportingMode::Absolute; ///< Absolute or relative reporting mode.
        std::vector<ComponentInfo> components{};           ///< Optional component metadata; defaults are generated when empty.
    };

    /// @brief Description used to create one synthetic device.
    struct SyntheticDeviceDescription
    {
        std::string name{};                                  ///< Human-readable UTF-8 device name.
        std::string persistentKey{};                         ///< Stable binding key for the synthetic device.
        std::vector<SyntheticElementDescription> elements{}; ///< Elements exposed by the device.
    };
} // namespace GameWIP::Input::Types

namespace GameWIP::Input::Synthetic
{
    /// @brief Explicit virtual-device producer for deterministic tests and API exploration.
    /// @details A Source is attached to one open System and must be used from
    /// that System's owner thread. It never appears implicitly and marks all
    /// published values with Synthetic provenance.
    class Source final
    {
    public:
        // ------------------------------------------------------------
        // Source lifecycle
        // ------------------------------------------------------------

        /// @brief Creates a detached synthetic source.
        INPUT_EXPORT Source() noexcept;

        /// @brief Detaches the source as a best-effort cleanup operation.
        /// @details Explicit detach is preferred when the status matters. A source destroyed on the wrong thread cannot mutate the attached System.
        INPUT_EXPORT ~Source() noexcept;

        Source(const Source &) = delete;

        Source &operator=(const Source &) = delete;

        Source(Source &&) = delete;

        Source &operator=(Source &&) = delete;

        // ------------------------------------------------------------
        // Device registration
        // ------------------------------------------------------------

        /// @brief Attaches this source to one open System.
        /// @param system Open owner-thread-affine System.
        /// @return Operation status.
        [[nodiscard]] INPUT_EXPORT GameWIP::Status::Types::Result<void> attach(System &system) noexcept;

        /// @brief Removes all synthetic devices and detaches from the System.
        /// @return Operation status.
        [[nodiscard]] INPUT_EXPORT GameWIP::Status::Types::Result<void> detach() noexcept;

        /// @brief Creates one virtual device from a descriptor.
        /// @param description Device and element metadata to publish.
        /// @return The new runtime device identity.
        [[nodiscard]] INPUT_EXPORT GameWIP::Status::Types::Result<Types::DeviceId> addDevice(
            const Types::SyntheticDeviceDescription &description) noexcept;

        /// @brief Disconnects one virtual device owned by this source.
        /// @param device Runtime device identity to remove.
        /// @return Operation status.
        [[nodiscard]] INPUT_EXPORT GameWIP::Status::Types::Result<void> removeDevice(Types::DeviceId device) noexcept;

        // ------------------------------------------------------------
        // Value observations
        // ------------------------------------------------------------

        /// @brief Publishes an observation for one element.
        /// @param element Runtime element identity to update.
        /// @param value Absolute value or relative delta, as selected by the element descriptor's reporting mode.
        /// @param when Optional observation timestamp; zero uses the portable clock.
        /// @return Operation status.
        /// @details Absolute elements replace their value; relative elements add this report to the current frame's accumulated value.
        [[nodiscard]] INPUT_EXPORT GameWIP::Status::Types::Result<void> setValue(
            Types::ElementId element,
            const Types::Value &value,
            Types::Timestamp when = {}) noexcept;

        /// @brief Replaces an absolute element value.
        /// @param element Runtime element identity to update.
        /// @param value Typed value matching an absolute element descriptor.
        /// @param when Optional observation timestamp; zero uses the portable clock.
        /// @return Operation status.
        [[nodiscard]] INPUT_EXPORT GameWIP::Status::Types::Result<void> setAbsoluteValue(
            Types::ElementId element,
            const Types::Value &value,
            Types::Timestamp when = {}) noexcept;

        /// @brief Adds a relative delta to an element for the current frame.
        /// @param element Runtime element identity to update.
        /// @param delta Typed delta matching a relative element descriptor.
        /// @param when Optional observation timestamp; zero uses the portable clock.
        /// @return Operation status.
        [[nodiscard]] INPUT_EXPORT GameWIP::Status::Types::Result<void> addRelativeDelta(
            Types::ElementId element,
            const Types::Value &delta,
            Types::Timestamp when = {}) noexcept;

        /// @brief Publishes a repeated report without creating a boolean edge.
        /// @param element Runtime element identity to repeat.
        /// @param when Optional observation timestamp; zero uses the portable clock.
        /// @return Operation status.
        [[nodiscard]] INPUT_EXPORT GameWIP::Status::Types::Result<void> repeat(Types::ElementId element, Types::Timestamp when = {}) noexcept;

        /// @brief Marks an element unavailable without manufacturing a value transition.
        /// @param element Runtime element identity to invalidate.
        /// @param when Optional observation timestamp; zero uses the portable clock.
        /// @return Operation status.
        [[nodiscard]] INPUT_EXPORT GameWIP::Status::Types::Result<void> invalidate(Types::ElementId element, Types::Timestamp when = {}) noexcept;

        // ------------------------------------------------------------
        // Text input
        // ------------------------------------------------------------

        /// @brief Commits validated UTF-8 text to a synthetic surface.
        /// @param surface Nonzero runtime surface identity.
        /// @param utf8 UTF-8 text to append to the current frame.
        /// @param when Optional observation timestamp; zero uses the portable clock.
        /// @return Operation status.
        [[nodiscard]] INPUT_EXPORT GameWIP::Status::Types::Result<void> commitText(
            Types::SurfaceId surface,
            std::string_view utf8,
            Types::Timestamp when = {}) noexcept;

    private:
        [[nodiscard]] GameWIP::Status::Types::Status attachStatus(System &system) noexcept;
        [[nodiscard]] GameWIP::Status::Types::Status detachStatus() noexcept;
        [[nodiscard]] GameWIP::Status::Types::Status addDeviceStatus(
            const Types::SyntheticDeviceDescription &description,
            Types::DeviceId &outDevice) noexcept;
        [[nodiscard]] GameWIP::Status::Types::Status removeDeviceStatus(Types::DeviceId device) noexcept;
        [[nodiscard]] GameWIP::Status::Types::Status setValueStatus(
            Types::ElementId element,
            const Types::Value &value,
            Types::Timestamp when) noexcept;
        [[nodiscard]] GameWIP::Status::Types::Status setAbsoluteValueStatus(
            Types::ElementId element,
            const Types::Value &value,
            Types::Timestamp when) noexcept;
        [[nodiscard]] GameWIP::Status::Types::Status addRelativeDeltaStatus(
            Types::ElementId element,
            const Types::Value &delta,
            Types::Timestamp when) noexcept;
        [[nodiscard]] GameWIP::Status::Types::Status repeatStatus(Types::ElementId element, Types::Timestamp when) noexcept;
        [[nodiscard]] GameWIP::Status::Types::Status invalidateStatus(Types::ElementId element, Types::Timestamp when) noexcept;
        [[nodiscard]] GameWIP::Status::Types::Status commitTextStatus(
            Types::SurfaceId surface,
            std::string_view utf8,
            Types::Timestamp when) noexcept;

        [[nodiscard]] GameWIP::Status::Types::Status checkAttached() const noexcept;
        [[nodiscard]] GameWIP::Status::Types::Status publishValue(
            Types::ElementId element,
            const Types::Value &value,
            std::optional<Types::ReportingMode> expectedReporting,
            Types::Timestamp when) noexcept;

        struct Impl;
        std::unique_ptr<Impl> impl_{};
    };
} // namespace GameWIP::Input::Synthetic
