/// @file synthetic.h
/// @brief Optional public virtual-device producer for deterministic tests.
#pragma once

#include "input/system.h"

#include <optional>
#include <string>
#include <string_view>

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
    class INPUT_EXPORT Source final
    {
    public:
        /// @brief Creates a detached synthetic source.
        Source() noexcept;

        /// @brief Detaches the source as a best-effort cleanup operation.
        /// @details Explicit detach is preferred when the status matters. A source destroyed on the wrong thread cannot mutate the attached System.
        ~Source() noexcept;

        Source(const Source &) = delete;

        Source &operator=(const Source &) = delete;

        Source(Source &&) = delete;

        Source &operator=(Source &&) = delete;

        /// @brief Attaches this source to one open System.
        /// @param system Open owner-thread-affine System.
        /// @return Success, AlreadyOpen, NotOpen, ResourceBusy, or OutOfMemory.
        [[nodiscard]] IO::Types::Status attach(System &system) noexcept;

        /// @brief Removes all synthetic devices and detaches from the System.
        /// @return Success, NotOpen, ResourceBusy, or OutOfMemory.
        [[nodiscard]] IO::Types::Status detach() noexcept;

        /// @brief Creates one virtual device from a descriptor.
        /// @param description Device and element metadata to publish.
        /// @param outDevice Receives the new runtime device identity.
        /// @return Success, InvalidArgument, NotOpen, ResourceBusy, or OutOfMemory.
        [[nodiscard]] IO::Types::Status addDevice(const Types::SyntheticDeviceDescription &description, Types::DeviceId &outDevice) noexcept;

        /// @brief Disconnects one virtual device owned by this source.
        /// @param device Runtime device identity to remove.
        /// @return Success, NotFound, NotOpen, ResourceBusy, or OutOfMemory.
        [[nodiscard]] IO::Types::Status removeDevice(Types::DeviceId device) noexcept;

        /// @brief Publishes an observation for one element.
        /// @param element Runtime element identity to update.
        /// @param value Absolute value or relative delta, as selected by the element descriptor's reporting mode.
        /// @param when Optional observation timestamp; zero uses the portable clock.
        /// @return Success, InvalidArgument, NotFound, NotOpen, ResourceBusy, or OutOfMemory.
        /// @details Absolute elements replace their value; relative elements add this report to the current frame's accumulated value.
        [[nodiscard]] IO::Types::Status setValue(Types::ElementId element, const Types::Value &value, Types::Timestamp when = {}) noexcept;

        /// @brief Replaces an absolute element value.
        /// @param element Runtime element identity to update.
        /// @param value Typed value matching an absolute element descriptor.
        /// @param when Optional observation timestamp; zero uses the portable clock.
        /// @return Success, InvalidArgument, NotFound, NotOpen, ResourceBusy, or OutOfMemory.
        [[nodiscard]] IO::Types::Status setAbsoluteValue(Types::ElementId element, const Types::Value &value, Types::Timestamp when = {}) noexcept;

        /// @brief Adds a relative delta to an element for the current frame.
        /// @param element Runtime element identity to update.
        /// @param delta Typed delta matching a relative element descriptor.
        /// @param when Optional observation timestamp; zero uses the portable clock.
        /// @return Success, InvalidArgument, NotFound, NotOpen, ResourceBusy, or OutOfMemory.
        [[nodiscard]] IO::Types::Status addRelativeDelta(Types::ElementId element, const Types::Value &delta, Types::Timestamp when = {}) noexcept;

        /// @brief Publishes a repeated report without creating a boolean edge.
        /// @param element Runtime element identity to repeat.
        /// @param when Optional observation timestamp; zero uses the portable clock.
        /// @return Success, InvalidArgument, NotFound, NotOpen, ResourceBusy, or OutOfMemory.
        [[nodiscard]] IO::Types::Status repeat(Types::ElementId element, Types::Timestamp when = {}) noexcept;

        /// @brief Marks an element unavailable without manufacturing a value transition.
        /// @param element Runtime element identity to invalidate.
        /// @param when Optional observation timestamp; zero uses the portable clock.
        /// @return Success, NotFound, NotOpen, ResourceBusy, or OutOfMemory.
        [[nodiscard]] IO::Types::Status invalidate(Types::ElementId element, Types::Timestamp when = {}) noexcept;

        /// @brief Commits validated UTF-8 text to a synthetic surface.
        /// @param surface Nonzero runtime surface identity.
        /// @param utf8 UTF-8 text to append to the current frame.
        /// @param when Optional observation timestamp; zero uses the portable clock.
        /// @return Success, EncodingFailed, InvalidArgument, Unsupported, NotOpen, ResourceBusy, or OutOfMemory.
        [[nodiscard]] IO::Types::Status commitText(Types::SurfaceId surface, std::string_view utf8, Types::Timestamp when = {}) noexcept;

    private:
        [[nodiscard]] IO::Types::Status checkAttached() const noexcept;
        [[nodiscard]] IO::Types::Status publishValue(
            Types::ElementId element,
            const Types::Value &value,
            std::optional<Types::ReportingMode> expectedReporting,
            Types::Timestamp when) noexcept;

        struct Impl;
        std::unique_ptr<Impl> impl_{};
    };
} // namespace GameWIP::Input::Synthetic
