/// @file drag_drop.h
/// @brief Optional native desktop data drag-and-drop source and target API.

#pragma once

#include "desktop/data_transfer.h"
#include "desktop/desktop_export.h"
#include "desktop/events.h"
#include "desktop/window.h"
#include "io/status.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <variant>
#include <vector>

namespace GameWIP::Desktop::Types::DragDrop
{
    // ------------------------------------------------------------
    // Identities and effects
    // ------------------------------------------------------------

    /// @brief Stable identity for one native drag session observed by a target.
    struct SessionId
    {
        std::uint64_t value = 0; ///< Non-zero identity; zero represents no session.

        /// @brief Returns whether this identity names a session.
        /// @return true when value is non-zero.
        [[nodiscard]] constexpr bool isValid() const noexcept
        {
            return value != 0;
        }
        /// @brief Compares two session identities.
        friend constexpr bool operator==(SessionId, SessionId) noexcept = default;
    };

    /// @brief Application-defined identity for one declarative target region.
    struct RegionId
    {
        std::uint64_t value = 0; ///< Non-zero application identity; zero means no region.

        /// @brief Returns whether this identity names a region.
        /// @return true when value is non-zero.
        [[nodiscard]] constexpr bool isValid() const noexcept
        {
            return value != 0;
        }
        /// @brief Compares two region identities.
        friend constexpr bool operator==(RegionId, RegionId) noexcept = default;
    };

    /// @brief Bitmask of native data-transfer effects.
    enum class Effect : std::uint8_t
    {
        None = 0,       ///< Reject the transfer or report no completed effect.
        Copy = 1U << 0, ///< Copy the transferred data.
        Move = 1U << 1, ///< Request source-defined move semantics after a successful drop.
        Link = 1U << 2  ///< Create a source-defined link to the transferred data.
    };

    /// @brief Combines two effect masks.
    /// @param left First effect mask.
    /// @param right Second effect mask.
    /// @return Union of both masks.
    [[nodiscard]] constexpr Effect operator|(Effect left, Effect right) noexcept
    {
        return static_cast<Effect>(static_cast<std::uint8_t>(left) | static_cast<std::uint8_t>(right));
    }
    /// @brief Intersects two effect masks.
    /// @param left First effect mask.
    /// @param right Second effect mask.
    /// @return Intersection of both masks.
    [[nodiscard]] constexpr Effect operator&(Effect left, Effect right) noexcept
    {
        return static_cast<Effect>(static_cast<std::uint8_t>(left) & static_cast<std::uint8_t>(right));
    }
    /// @brief Adds an effect mask to an existing mask.
    /// @param left Effect mask to update.
    /// @param right Effect mask to add.
    /// @return Reference to the updated mask.
    constexpr Effect &operator|=(Effect &left, Effect right) noexcept
    {
        return left = left | right;
    }
    /// @brief Intersects an existing effect mask in place.
    /// @param left Effect mask to update.
    /// @param right Effect mask to intersect.
    /// @return Reference to the updated mask.
    constexpr Effect &operator&=(Effect &left, Effect right) noexcept
    {
        return left = left & right;
    }

    // ------------------------------------------------------------
    // Source and target descriptions
    // ------------------------------------------------------------

    /// @brief Terminal result of a source-side native drag operation.
    enum class Outcome : std::uint8_t
    {
        Dropped,  ///< A target accepted and completed the drop.
        Cancelled ///< The drag ended without an accepted drop.
    };

    /// @brief Mouse button whose release completes the source-side drag.
    enum class TriggerButton : std::uint8_t
    {
        Left,  ///< Primary mouse button.
        Right, ///< Secondary mouse button.
        Middle ///< Middle mouse button.
    };

    /// @brief One logical client region and its declarative acceptance policy.
    /// @details An empty rect means the whole client and follows client resize automatically.
    struct RegionDescription
    {
        RegionId id;                                       ///< Required unique, non-zero region identity.
        std::optional<LogicalRect> rect;                   ///< Client-relative logical bounds, or the whole client.
        std::span<const DataTransfer::FormatView> formats; ///< Accepted formats in application preference order.
        Effect allowedEffects = Effect::Copy;              ///< Non-empty subset of Copy, Move, and Link.
        Effect preferredEffect = Effect::Copy;             ///< Single preferred effect contained in allowedEffects.
    };

    /// @brief Initial immutable region snapshot for a native target.
    struct TargetDescription
    {
        std::span<const RegionDescription> regions; ///< Snapshot copied by open; must not be empty.
    };

    /// @brief Source data and explicit native drag policy.
    struct Description
    {
        std::span<const DataTransfer::ItemView> items;     ///< Items snapshotted before native modal dispatch begins.
        Effect allowedEffects = Effect::Copy;              ///< Non-empty subset of Copy, Move, and Link.
        TriggerButton triggerButton = TriggerButton::Left; ///< Button whose release completes the drag.
    };

    /// @brief Source-side native drag result.
    struct Result
    {
        IO::Types::Status status;             ///< Preparation or native dispatch status.
        Outcome outcome = Outcome::Cancelled; ///< Dropped only when a target completed the operation.
        Effect effect = Effect::None;         ///< Target-selected effect, or None when cancelled or failed.
    };

    /// @brief Immutable format metadata shared by all movement events in a native session.
    using FormatSnapshot = std::shared_ptr<const std::vector<DataTransfer::Format>>;

    // ------------------------------------------------------------
    // Target events
    // ------------------------------------------------------------

    namespace Events
    {
        /// @brief Reports a native drag session entering the top-level target.
        struct Entered
        {
            SessionId sessionId;          ///< Identity shared by all events in this native session.
            LogicalPosition position;     ///< Current client-relative logical pointer position.
            RegionId region;              ///< Matching accepted region, or invalid when no region currently matches.
            Effect effect = Effect::None; ///< Effect negotiated for this position.
            FormatSnapshot formats;       ///< Immutable offered-format metadata for the session.
        };

        /// @brief Reports movement within or between accepted regions.
        struct Moved
        {
            SessionId sessionId;          ///< Identity shared by all events in this native session.
            LogicalPosition position;     ///< Current client-relative logical pointer position.
            RegionId previousRegion;      ///< Previous matching region, or invalid when none matched.
            RegionId region;              ///< Current matching region, or invalid when none matches.
            Effect effect = Effect::None; ///< Effect negotiated for this position.
            FormatSnapshot formats;       ///< Immutable offered-format metadata for the session.
        };

        /// @brief Reports a native drag session leaving the top-level target without dropping.
        struct Left
        {
            SessionId sessionId; ///< Identity of the session that left.
        };

        /// @brief Reports a successful, fully materialized drop.
        struct Dropped
        {
            SessionId sessionId;           ///< Identity of the completed session.
            LogicalPosition position;      ///< Client-relative logical drop position.
            RegionId region;               ///< Region that accepted the drop.
            Effect effect = Effect::None;  ///< Effect negotiated for the completed drop.
            DataTransfer::Payload payload; ///< Owned portable payload, materialized transactionally.
        };

        /// @brief Tagged payload for one target-side drag-and-drop event.
        using Payload = std::variant<Entered, Moved, Left, Dropped>;
    } // namespace Events

    /// @brief One queued target event with a per-open monotonic sequence.
    struct Event
    {
        std::uint64_t sequence = 0; ///< Monotonic sequence within the current open lifetime.
        Events::Payload data;       ///< Tagged event payload.

        /// @brief Returns the mutable payload when it has the requested type.
        /// @tparam PayloadType One alternative in Events::Payload.
        /// @return Pointer to the stored payload, or nullptr when the type does not match.
        template <typename PayloadType> [[nodiscard]] PayloadType *getIf() noexcept
        {
            return std::get_if<PayloadType>(&data);
        }

        /// @brief Returns the immutable payload when it has the requested type.
        /// @tparam PayloadType One alternative in Events::Payload.
        /// @return Pointer to the stored payload, or nullptr when the type does not match.
        template <typename PayloadType> [[nodiscard]] const PayloadType *getIf() const noexcept
        {
            return std::get_if<PayloadType>(&data);
        }
    };
} // namespace GameWIP::Desktop::Types::DragDrop

namespace GameWIP::Desktop
{
    namespace Detail
    {
        struct DragDropAccess;
        struct DragDropState;
    } // namespace Detail

    // ------------------------------------------------------------
    // Target owner
    // ------------------------------------------------------------

    /// @brief Non-copyable, non-movable RAII owner of one native data drop target.
    /// @details A successful open inherits the Window owner thread. Descriptions and format names
    /// are copied during open or setRegions; caller-provided event storage remains borrowed until
    /// close. Native operations and event consumption require the inherited owner thread.
    class GAMEWIP_DESKTOP_EXPORT DragDropTarget final
    {
    public:
        // ------------------------------------------------------------
        // Lifecycle
        // ------------------------------------------------------------

        /// @name Lifecycle
        /// @{

        /// @brief Constructs a closed native drop-target owner.
        DragDropTarget() noexcept;
        DragDropTarget(const DragDropTarget &) = delete;
        DragDropTarget &operator=(const DragDropTarget &) = delete;
        DragDropTarget(DragDropTarget &&) = delete;
        DragDropTarget &operator=(DragDropTarget &&) = delete;
        /// @brief Revokes native registration and releases retained portable state.
        ~DragDropTarget() noexcept;

        /// @brief Opens with the default internally owned event-queue capacity.
        /// @param window Open Window whose owner thread and client area are inherited.
        /// @param description Initial declarative region snapshot.
        /// @return Success, or a status explaining why no target was registered.
        [[nodiscard]] IO::Types::Status open(Window &window, const Types::DragDrop::TargetDescription &description) noexcept;
        /// @brief Opens with a requested number of internally owned event slots.
        /// @param window Open Window whose owner thread and client area are inherited.
        /// @param description Initial declarative region snapshot.
        /// @param eventQueueCapacity Number of event slots to allocate; must be greater than zero.
        /// @return Success, or a status explaining why no target was registered.
        [[nodiscard]] IO::Types::Status open(
            Window &window,
            const Types::DragDrop::TargetDescription &description,
            std::size_t eventQueueCapacity) noexcept;
        /// @brief Opens while borrowing caller-owned event storage until close.
        /// @param window Open Window whose owner thread and client area are inherited.
        /// @param description Initial declarative region snapshot.
        /// @param eventStorage Non-empty storage that must remain alive and unmoved until close.
        /// @return Success, or a status explaining why no target was registered.
        [[nodiscard]] IO::Types::Status open(
            Window &window,
            const Types::DragDrop::TargetDescription &description,
            std::span<Types::DragDrop::Event> eventStorage) noexcept;
        /// @brief Returns whether the native target registration is usable.
        /// @return true while native operations are available; otherwise false.
        [[nodiscard]] bool isOpen() const noexcept;
        /// @brief Returns the complete portable native-resource lifecycle state.
        /// @return Current state, including pending owner-thread finalization.
        [[nodiscard]] Types::LifetimeState lifetimeState() const noexcept;
        /// @brief Closes on the owner thread, or releases portable pending state after dispatcher termination.
        /// @return Success when closed or already closed; ResourceBusy when native work belongs to another thread; otherwise cleanup failure.
        [[nodiscard]] IO::Types::Status close() noexcept;
        /// @}

        // ------------------------------------------------------------
        // Window and ownership
        // ------------------------------------------------------------

        /// @name Window and ownership
        /// @{

        /// @brief Returns the Window open-lifetime identity retained for this target.
        /// @return Window identity, including during pending finalization, or invalid while closed.
        [[nodiscard]] Types::WindowId windowId() const noexcept;
        /// @brief Returns whether the calling thread owns the current lifetime.
        /// @return true on the inherited owner thread while retained state exists.
        [[nodiscard]] bool ownedByCurrentThread() const noexcept;
        /// @}

        // ------------------------------------------------------------
        // Regions
        // ------------------------------------------------------------

        /// @name Regions
        /// @{

        /// @brief Atomically replaces the declarative region snapshot.
        /// @param regions Non-empty region descriptions copied before this call returns.
        /// @return Success, or validation/allocation failure with the previous snapshot unchanged.
        [[nodiscard]] IO::Types::Status setRegions(std::span<const Types::DragDrop::RegionDescription> regions) noexcept;
        /// @}

        // ------------------------------------------------------------
        // Event queue
        // ------------------------------------------------------------

        /// @name Event queue
        /// @{

        /// @brief Removes the oldest queued event on the owner thread.
        /// @param outEvent Destination replaced with the oldest event when available.
        /// @return true when an event was removed; otherwise false.
        [[nodiscard]] bool popEvent(Types::DragDrop::Event &outEvent) noexcept;
        /// @brief Removes up to destination.size() queued events in order.
        /// @param destination Caller-owned output slots.
        /// @return Number of events removed.
        [[nodiscard]] std::size_t popEvents(std::span<Types::DragDrop::Event> destination) noexcept;
        /// @brief Discards all queued events and releases retained drop payloads on the owner thread.
        void clearEvents() noexcept;
        /// @brief Returns cached event-queue ownership, capacity, and counters.
        /// @return Queue snapshot, or default values while closed.
        [[nodiscard]] Types::Events::QueueInfo eventQueueInfo() const noexcept;
        /// @brief Resets the dropped-event counter on the owner thread.
        void clearDroppedEventCount() noexcept;
        /// @}

    private:
        friend struct Detail::DragDropAccess;
        std::unique_ptr<Detail::DragDropState> state_;
    };
} // namespace GameWIP::Desktop

namespace GameWIP::Desktop::DragDrop
{
    // ------------------------------------------------------------
    // Source operation
    // ------------------------------------------------------------

    /// @brief Runs one synchronous native data drag from an open Window.
    /// @details All item data is validated and snapshotted before native modal dispatch. The call
    /// pumps the platform drag loop until a drop, cancellation, or native failure occurs. Move is
    /// only a negotiated result; this function never deletes or mutates source data.
    /// @param source Open Window owned by the calling thread.
    /// @param description Portable data and explicit source effect/button policy.
    /// @return Preparation/native status, terminal outcome, and the selected effect when dropped.
    [[nodiscard]] GAMEWIP_DESKTOP_EXPORT Types::DragDrop::Result beginDrag(Window &source, const Types::DragDrop::Description &description) noexcept;
} // namespace GameWIP::Desktop::DragDrop
