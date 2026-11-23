/// @file facade.h
/// @brief Window-integrated accessibility bridge.

#pragma once

#include "desktop/accessibility/snapshot.h"
#include "desktop/desktop_export.h"
#include "desktop/window.h"
#include "io/status.h"

namespace GameWIP::Desktop::Accessibility
{
    /// ------------------------------------------------------------
    /// Window accessibility bridge
    /// ------------------------------------------------------------

    /// @brief Window-integrated façade for the optional accessibility bridge.
    /// @details This is a lightweight non-owning view. Publication and inspection are safe from
    /// any thread after enable(); enable(), disable(), and action draining belong to the Window's
    /// owner thread. No application callback is ever invoked by the native provider.
    /// Initial enable() must complete before sharing the façade across threads. Join all façade users before
    /// destroying the borrowed Window. Concurrent publication may race close; the close gate prevents resurrection.
    /// There is no worker thread, UI toolkit dependency, rendering ownership, or semantic action execution.
    class DESKTOP_EXPORT Facade final
    {
    public:
        /// @name Activation
        /// @{

        explicit Facade(Window &window) noexcept; ///< Borrows Window without allocating; equivalent to Window::accessibility().

        /// @brief Explicitly enables the bridge for the current open Window lifetime.
        /// @return NotOpen, ResourceBusy on the wrong thread, AlreadyOpen, Unsupported, InvalidArgument for limits,
        /// or a resource failure. Failed enablement never exposes a native semantic tree.
        [[nodiscard]] IO::Types::Status enable(const Types::Accessibility::Options &options = {}) noexcept;

        /// @brief Disables the bridge and clears its active snapshot and queues.
        /// @return Success when already disabled; ResourceBusy for an enabled bridge called off its owner thread.
        /// @note Retained native providers become unavailable; retained portable readers remain usable.
        [[nodiscard]] IO::Types::Status disable() noexcept;

        /// @brief Reports whether the bridge is enabled for the current open lifetime.
        [[nodiscard]] bool enabled() const noexcept;

        /// @brief Returns native and portable feature support for the active bridge.
        [[nodiscard]] Types::Accessibility::Features features() const noexcept;

        /// @}

        /// @name Snapshot publication and inspection
        /// @{

        /// @brief Validates and copies a borrowed snapshot into immutable publication storage.
        /// @details Only a strictly newer nonzero generation may replace the active snapshot;
        /// generation order remains monotonic across this Window object's reopen cycles.
        /// Root identity is fixed until disable/close. Validation, ownership, UTF-16 indexes, and native preparation
        /// finish before replacement. A failed publication preserves the active tree. Notification pressure does
        /// not fail publication: it collapses diffs to a committed-generation invalidation marker.
        /// @return NotOpen for a closed gate; InvalidArgument for generation/root/semantic errors; SizeLimitExceeded
        /// for configured limits; OutOfMemory or a backend failure for preparation errors.
        [[nodiscard]] IO::Types::Status publish(const Types::Accessibility::SnapshotView &snapshot) noexcept;

        /// @brief Validates and copies an owning builder's current view.
        [[nodiscard]] IO::Types::Status publish(const SnapshotBuilder &snapshot) noexcept;

        /// @brief Returns metadata for the active snapshot without copying its tree.
        [[nodiscard]] Types::Accessibility::SnapshotInfo snapshotInfo() const noexcept;

        /// @brief Retains a read-only structural reader for allocation-free inspection.
        [[nodiscard]] SnapshotReader readSnapshot() const noexcept;

        /// @brief Deep-copies the active snapshot into caller-owned builder storage.
        /// @return NotOpen when no active snapshot; otherwise authoring-copy status. Destination is unchanged on failure.
        [[nodiscard]] IO::Types::Status copySnapshot(SnapshotBuilder &destination) const noexcept;

        /// @}

        /// @name Actions and notifications
        /// @{

        /// @brief Removes the oldest queued native action for owner-thread application handling.
        /// @return False without changing outAction when disabled, empty, or called on the wrong thread.
        /// @note Application code must revalidate node/generation and apply the action itself; acceptance is transport-only.
        [[nodiscard]] bool popAction(Types::Accessibility::ActionRequest &outAction) noexcept;

        /// @brief Returns bounded action and notification queue telemetry.
        /// @note Locks each queue separately; this is not a coherent joint snapshot of both queues.
        [[nodiscard]] Types::Accessibility::QueueInfo queueInfo() const noexcept;

        /// @brief Queues one bounded explicit announcement for native delivery.
        /// @return NotOpen, Unsupported (including unsupported language override), InvalidArgument, EncodingFailed,
        /// SizeLimitExceeded for full queues, or OutOfMemory. Delivery requires subsequent owner event pumping;
        /// acceptance does not guarantee that an assistive client speaks it. Native redaction is enforced at delivery.
        [[nodiscard]] IO::Types::Status announce(const Types::Accessibility::AnnouncementView &announcement) noexcept;
        /// @}

    private:
        Window *window_ = nullptr;
    };
} // namespace GameWIP::Desktop::Accessibility
