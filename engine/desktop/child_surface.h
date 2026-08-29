/// @file child_surface.h
/// @brief Optional managed native child-host API for GameWIP Window.

#pragma once

#include "desktop/events.h"
#include "desktop/window.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <variant>

namespace GameWIP::Desktop::Types::ChildSurface
{
    /// @brief Initial properties of a native child host.
    /// @details rect is relative to the parent Window client origin. Zero extents, negative
    /// positions, and rectangles extending beyond the parent are valid.
    struct Description
    {
        LogicalRect rect;                   ///< Authoritative parent-relative logical rectangle.
        bool visible = false;               ///< Whether the host is initially visible.
        bool userInteractionEnabled = true; ///< Whether normal native interaction is initially enabled.
    };

    /// @brief Typed native child-host event payloads.
    namespace Events
    {
        /// @brief Reports unexpected loss of the native host.
        struct NativeDestroyed
        {
        };
        /// @brief Reports a parent-relative logical position change.
        struct PositionChanged
        {
            LogicalPosition position; ///< New parent-relative logical position.
        };
        /// @brief Reports a logical extent change.
        struct SizeChanged
        {
            LogicalSize size; ///< New logical extent.
        };
        /// @brief Reports a physical host extent change.
        struct PixelSizeChanged
        {
            PixelSize size; ///< New physical extent.
        };
        /// @brief Reports native visibility changing.
        struct VisibilityChanged
        {
            bool visible = false; ///< New visibility state.
        };
        /// @brief Reports an effective-DPI transition while preserving logical geometry.
        struct ContentScaleChanged
        {
            ContentScale previousScale; ///< Scale before the transition.
            ContentScale scale;         ///< Scale after the transition.
            Dpi previousDpi;            ///< Effective DPI before the transition.
            Dpi dpi;                    ///< Effective DPI after the transition.
            PixelSize pixelSize;        ///< Physical extent after the transition.
        };

        /// @brief Tagged payload for one ChildSurface event.
        using Payload = std::variant<NativeDestroyed, PositionChanged, SizeChanged, PixelSizeChanged, VisibilityChanged, ContentScaleChanged>;
    } // namespace Events

    /// @brief One queued ChildSurface event with a per-open monotonic sequence.
    struct Event
    {
        std::uint64_t sequence = 0; ///< Monotonic sequence within the current open lifetime.
        Events::Payload data;       ///< Tagged event payload.

        /// @brief Returns the mutable payload when it has the requested type.
        template <typename PayloadType> [[nodiscard]] PayloadType *getIf() noexcept
        {
            return std::get_if<PayloadType>(&data);
        }

        /// @brief Returns the immutable payload when it has the requested type.
        template <typename PayloadType> [[nodiscard]] const PayloadType *getIf() const noexcept
        {
            return std::get_if<PayloadType>(&data);
        }
    };
} // namespace GameWIP::Desktop::Types::ChildSurface

namespace GameWIP::Desktop
{
    namespace Detail
    {
        struct ChildSurfaceAccess;
        struct ChildSurfaceState;
    } // namespace Detail

    /// @brief Non-copyable, non-movable RAII owner of one native child host.
    /// @details A successful open inherits the parent Window owner thread. Cached getters are
    /// unsynchronized; native operations and event consumption require that owner thread.
    class GAMEWIP_DESKTOP_EXPORT ChildSurface final
    {
    public:
        // ------------------------------------------------------------
        // Lifecycle
        // ------------------------------------------------------------

        /// @name Lifecycle
        /// @{

        /// @brief Constructs a closed ChildSurface owner.
        ChildSurface() noexcept;
        ChildSurface(const ChildSurface &) = delete;
        ChildSurface &operator=(const ChildSurface &) = delete;
        ChildSurface(ChildSurface &&) = delete;
        ChildSurface &operator=(ChildSurface &&) = delete;
        /// @brief Closes the native host and releases retained portable state.
        ~ChildSurface() noexcept;

        /// @brief Opens with the default internal event-queue capacity.
        /// @param parent Open top-level Window whose owner thread and client origin are inherited.
        /// @param description Initial logical geometry, visibility, and interaction state.
        /// @return Success, or a status explaining why no native host was opened.
        [[nodiscard]] IO::Types::Status open(Window &parent, const Types::ChildSurface::Description &description = {}) noexcept;
        /// @brief Opens with a requested number of internally owned event slots.
        /// @param parent Open top-level Window whose owner thread and client origin are inherited.
        /// @param description Initial logical geometry, visibility, and interaction state.
        /// @param eventQueueCapacity Number of event slots to allocate; must be greater than zero.
        /// @return Success, or a status explaining why no native host was opened.
        [[nodiscard]] IO::Types::Status open(
            Window &parent,
            const Types::ChildSurface::Description &description,
            std::size_t eventQueueCapacity) noexcept;
        /// @brief Opens while borrowing caller-owned event storage until close.
        /// @param parent Open top-level Window whose owner thread and client origin are inherited.
        /// @param description Initial logical geometry, visibility, and interaction state.
        /// @param eventStorage Non-empty storage that must remain alive and unmoved until close.
        /// @return Success, or a status explaining why no native host was opened.
        [[nodiscard]] IO::Types::Status open(
            Window &parent,
            const Types::ChildSurface::Description &description,
            std::span<Types::ChildSurface::Event> eventStorage) noexcept;
        /// @brief Returns whether the native host is usable.
        /// @return true while native operations are available; otherwise false.
        [[nodiscard]] bool isOpen() const noexcept;
        /// @brief Returns the complete portable native-resource lifecycle state.
        /// @return Current state, including pending owner-thread finalization.
        [[nodiscard]] Types::LifetimeState lifetimeState() const noexcept;
        /// @brief Closes or finalizes the current lifetime on its owner thread.
        /// @return Success when closed or already closed; ResourceBusy on the wrong thread; otherwise cleanup failure.
        [[nodiscard]] IO::Types::Status close() noexcept;
        /// @}

        // ------------------------------------------------------------
        // Parent and ownership
        // ------------------------------------------------------------

        /// @name Parent and ownership
        /// @{

        /// @brief Returns the parent Window open-lifetime identity retained for this lifetime.
        /// @return Parent identity, including during pending finalization, or an invalid identity while closed.
        [[nodiscard]] Types::WindowId parentId() const noexcept;
        /// @brief Returns whether the calling thread owns the current lifetime.
        /// @return true on the inherited owner thread while retained state exists.
        [[nodiscard]] bool ownedByCurrentThread() const noexcept;
        /// @}

        // ------------------------------------------------------------
        // Event queue
        // ------------------------------------------------------------

        /// @name Event queue
        /// @{

        /// @brief Removes the oldest queued event on the owner thread.
        /// @param outEvent Destination replaced with the oldest event when available.
        /// @return true when an event was removed; otherwise false.
        [[nodiscard]] bool popEvent(Types::ChildSurface::Event &outEvent) noexcept;
        /// @brief Removes up to destination.size() queued events in order.
        /// @param destination Caller-owned output slots.
        /// @return Number of events removed.
        [[nodiscard]] std::size_t popEvents(std::span<Types::ChildSurface::Event> destination) noexcept;
        /// @brief Discards all queued events on the owner thread.
        void clearEvents() noexcept;
        /// @brief Returns cached event-queue ownership, capacity, and counters.
        /// @return Queue snapshot, or default values while closed.
        [[nodiscard]] Types::Events::QueueInfo eventQueueInfo() const noexcept;
        /// @brief Resets the dropped-event counter on the owner thread.
        void clearDroppedEventCount() noexcept;
        /// @}

        // ------------------------------------------------------------
        // Cached state
        // ------------------------------------------------------------

        /// @name Cached state
        /// @{

        /// @brief Returns the authoritative parent-relative logical rectangle.
        /// @return Cached rectangle, or zero geometry while closed.
        [[nodiscard]] Types::LogicalRect rect() const noexcept;
        /// @brief Returns the authoritative parent-relative logical position.
        /// @return Cached position, or zero while closed.
        [[nodiscard]] Types::LogicalPosition position() const noexcept;
        /// @brief Returns the authoritative logical extent.
        /// @return Cached extent, or zero while closed.
        [[nodiscard]] Types::LogicalSize size() const noexcept;
        /// @brief Returns the current physical host extent.
        /// @return Cached physical extent, or zero while closed.
        [[nodiscard]] Types::PixelSize pixelSize() const noexcept;
        /// @brief Returns physical host geometry in virtual-screen coordinates.
        /// @return Cached physical screen rectangle, or zero geometry while closed.
        [[nodiscard]] Types::ScreenRect screenRect() const noexcept;
        /// @brief Returns physical pixels per baseline logical unit.
        /// @return Cached scale, or the default scale while closed.
        [[nodiscard]] Types::ContentScale contentScale() const noexcept;
        /// @brief Returns the effective DPI snapshot.
        /// @return Cached effective DPI, or zero while closed.
        [[nodiscard]] Types::Dpi effectiveDpi() const noexcept;
        /// @brief Returns whether native visibility is requested for the host.
        /// @return true when the host has native visible style; otherwise false.
        [[nodiscard]] bool visible() const noexcept;
        /// @brief Returns whether normal native interaction is enabled.
        /// @return true when interaction is enabled; otherwise false.
        [[nodiscard]] bool userInteractionEnabled() const noexcept;
        /// @}

        // ------------------------------------------------------------
        // Geometry
        // ------------------------------------------------------------

        /// @name Geometry
        /// @{

        /// @brief Changes parent-relative logical position and extent together.
        /// @param rect New authoritative rectangle; zero extents and negative positions are valid.
        /// @return Success or the validation/native failure.
        [[nodiscard]] IO::Types::Status setRect(Types::LogicalRect rect) noexcept;
        /// @brief Changes only the parent-relative logical position.
        /// @param position New position; negative coordinates are valid.
        /// @return Success or the validation/native failure.
        [[nodiscard]] IO::Types::Status setPosition(Types::LogicalPosition position) noexcept;
        /// @brief Changes only the logical extent.
        /// @param size New extent; zero dimensions are valid.
        /// @return Success or the validation/native failure.
        [[nodiscard]] IO::Types::Status setSize(Types::LogicalSize size) noexcept;
        /// @}

        // ------------------------------------------------------------
        // Coordinate conversion
        // ------------------------------------------------------------

        /// @name Coordinate conversion
        /// @{

        /// @brief Converts a ChildSurface-local logical position to physical virtual-screen coordinates.
        /// @param position Logical position relative to the ChildSurface client origin.
        /// @return Conversion status and physical screen position.
        [[nodiscard]] Types::ScreenPositionResult clientToScreen(Types::LogicalPosition position) const noexcept;
        /// @brief Converts a physical virtual-screen position to ChildSurface-local logical coordinates.
        /// @param position Physical virtual-screen position.
        /// @return Conversion status and logical client-local position.
        [[nodiscard]] Types::LogicalPositionResult screenToClient(Types::ScreenPosition position) const noexcept;
        /// @}

        // ------------------------------------------------------------
        // Visibility and interaction
        // ------------------------------------------------------------

        /// @name Visibility and interaction
        /// @{

        /// @brief Makes the native host visible without requesting activation.
        /// @return Success or the lifecycle/native failure.
        [[nodiscard]] IO::Types::Status show() noexcept;
        /// @brief Hides the native host.
        /// @return Success or the lifecycle/native failure.
        [[nodiscard]] IO::Types::Status hide() noexcept;
        /// @brief Enables or disables normal native interaction for the host and descendants.
        /// @param enabled Whether interaction should be enabled.
        /// @return Success or the lifecycle/native failure.
        [[nodiscard]] IO::Types::Status setUserInteractionEnabled(bool enabled) noexcept;
        /// @}

        // ------------------------------------------------------------
        // Native sibling ordering
        // ------------------------------------------------------------

        /// @name Native sibling ordering
        /// @{

        /// @brief Places this host at the front of its native sibling order.
        /// @return Success or the lifecycle/native failure.
        [[nodiscard]] IO::Types::Status bringToFront() noexcept;
        /// @brief Places this host at the back of its native sibling order.
        /// @return Success or the lifecycle/native failure.
        [[nodiscard]] IO::Types::Status sendToBack() noexcept;
        /// @brief Places this host immediately above a compatible native sibling.
        /// @param sibling Different open ChildSurface in the same parent lifetime and owner thread.
        /// @return Success, InvalidArgument for an incompatible sibling, or lifecycle/native failure.
        [[nodiscard]] IO::Types::Status placeAbove(const ChildSurface &sibling) noexcept;
        /// @brief Places this host immediately below a compatible native sibling.
        /// @param sibling Different open ChildSurface in the same parent lifetime and owner thread.
        /// @return Success, InvalidArgument for an incompatible sibling, or lifecycle/native failure.
        [[nodiscard]] IO::Types::Status placeBelow(const ChildSurface &sibling) noexcept;
        /// @}

    private:
        friend struct Detail::ChildSurfaceAccess;
        std::unique_ptr<Detail::ChildSurfaceState> state_;
    };
} // namespace GameWIP::Desktop
