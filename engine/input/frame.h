/// @file frame.h
/// @brief Caller-owned coherent Input observation snapshot and interval events.
#pragma once

#include "input/types.h"

// The Input target generates this header. Keep the fallback for source-tree
// checks when the generated header is unavailable.
#if defined(__has_include)
#if __has_include("input/input_export.h")
#include "input/input_export.h"
#endif
#endif
#ifndef INPUT_EXPORT
#define INPUT_EXPORT
#endif

#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace GameWIP::Input
{
    class System;
}

/// @brief Frame and snapshot types for the GameWIP Input API.
namespace GameWIP::Input::Types
{
    /// @brief One optional native report retained for diagnostics or replay.
    struct RawReport
    {
        DeviceId device{};                            ///< Device that produced the report.
        BackendKind backend = BackendKind::Synthetic; ///< Backend that produced the report.
        Timestamp timestamp{};                        ///< Native and portable receipt time.
        std::vector<std::byte> bytes{};               ///< Raw report bytes in backend-defined format.
    };

    /// @brief Caller-owned snapshot of latest state and interval events.
    /// @details A Frame owns all storage returned by its accessors. Spans and string views remain valid until the frame is modified or destroyed.
    class INPUT_EXPORT Frame final
    {
    public:
        /// @brief Creates an empty frame.
        Frame() = default;

        /// @brief Copies a complete frame and its owned storage.
        Frame(const Frame &) = default;

        /// @brief Assigns a complete frame and its owned storage.
        /// @return This frame.
        Frame &operator=(const Frame &) = default;

        /// @brief Moves a complete frame and its owned storage.
        Frame(Frame &&) noexcept = default;

        /// @brief Moves frame storage into this frame.
        /// @return This frame.
        Frame &operator=(Frame &&) noexcept = default;

        /// @brief Destroys the frame and its owned storage.
        ~Frame() = default;

        /// @brief Returns the capture sequence assigned to this frame.
        /// @return Monotonic capture sequence, or zero for an empty frame.
        [[nodiscard]] std::uint64_t sequence() const noexcept
        {
            return sequence_;
        }

        /// @brief Returns the device-descriptor revision observed at capture.
        /// @return Descriptor revision associated with this frame.
        [[nodiscard]] std::uint64_t deviceRevision() const noexcept
        {
            return deviceRevision_;
        }

        /// @brief Returns the portable capture time.
        /// @return Steady-clock time at which the frame was published.
        [[nodiscard]] Clock::time_point capturedAt() const noexcept
        {
            return capturedAt_;
        }

        /// @brief Reports whether this interval contains a discontinuity.
        /// @return `true` when dropped or invalid reports, event loss, or source recovery may have made the interval incomplete.
        [[nodiscard]] bool discontinuity() const noexcept
        {
            return discontinuity_;
        }

        /// @brief Returns the number of events lost before this frame.
        /// @return Interval event-loss count.
        [[nodiscard]] std::uint64_t droppedEvents() const noexcept
        {
            return droppedEvents_;
        }

        /// @brief Returns all latest element states owned by the frame.
        /// @return Read-only state span valid while this frame is unchanged.
        [[nodiscard]] std::span<const ElementState> states() const noexcept
        {
            return states_;
        }

        /// @brief Returns events observed since the previous capture.
        /// @return Read-only event span in sequence order.
        [[nodiscard]] std::span<const Event> events() const noexcept
        {
            return events_;
        }

        /// @brief Returns retained native reports.
        /// @return Read-only raw-report span, empty when raw capture is disabled or no report was retained.
        [[nodiscard]] std::span<const RawReport> rawReports() const noexcept
        {
            return rawReports_;
        }

        /// @brief Returns committed text observed since the previous capture.
        /// @return UTF-8 text view valid while this frame is unchanged.
        [[nodiscard]] std::string_view textUtf8() const noexcept
        {
            return textUtf8_;
        }

        /// @brief Finds the latest state for a runtime element ID.
        /// @param id Element ID to find.
        /// @return Pointer into this frame, or null when the ID is absent.
        [[nodiscard]] const ElementState *state(ElementId id) const noexcept;

        /// @brief Finds the latest typed value for a runtime element ID.
        /// @param id Element ID to find.
        /// @return Pointer into this frame, or null when the ID is absent.
        [[nodiscard]] const Value *value(ElementId id) const noexcept;

        /// @brief Returns one numeric component from a scalar or vector value.
        /// @param id Component identity and zero-based index.
        /// @return Numeric component, or no value for a nonnumeric or out-of-range component.
        [[nodiscard]] std::optional<Scalar> component(ComponentId id) const noexcept;

        /// @brief Reports whether a valid boolean element is currently true.
        /// @param id Element ID to inspect.
        /// @return `true` only for a valid boolean value that is true.
        [[nodiscard]] bool isDown(ElementId id) const noexcept;

        /// @brief Reports whether a boolean element became true in this interval.
        /// @param id Element ID to inspect.
        /// @return `true` when at least one press transition occurred.
        [[nodiscard]] bool wasPressed(ElementId id) const noexcept;

        /// @brief Reports whether a boolean element became false in this interval.
        /// @param id Element ID to inspect.
        /// @return `true` when at least one release transition occurred.
        [[nodiscard]] bool wasReleased(ElementId id) const noexcept;

        /// @brief Returns the number of boolean press transitions in this interval.
        /// @param id Element ID to inspect.
        /// @return Press transition count, or zero when the ID is absent or non-boolean.
        [[nodiscard]] std::uint32_t pressCount(ElementId id) const noexcept;

        /// @brief Returns the number of boolean release transitions in this interval.
        /// @param id Element ID to inspect.
        /// @return Release transition count, or zero when the ID is absent or non-boolean.
        [[nodiscard]] std::uint32_t releaseCount(ElementId id) const noexcept;

    private:
        friend class GameWIP::Input::System;

        std::uint64_t sequence_ = 0;
        std::uint64_t deviceRevision_ = 0;
        Clock::time_point capturedAt_{};
        bool discontinuity_ = false;
        std::uint64_t droppedEvents_ = 0;
        std::vector<ElementState> states_{};
        std::vector<Event> events_{};
        std::vector<RawReport> rawReports_{};
        std::string textUtf8_{};
    };
} // namespace GameWIP::Input::Types
