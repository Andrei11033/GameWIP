/// @file snapshot.h
/// @brief Accessibility snapshot validation, authoring, and inspection.

#pragma once

#include "desktop/accessibility/types.h"
#include "desktop/desktop_export.h"
#include "status/result.h"

#include <memory>

namespace GameWIP::Desktop::Internal
{
    struct PublishedSnapshot;
    struct SnapshotBuilderState;
} // namespace GameWIP::Desktop::Internal

namespace GameWIP::Desktop::Accessibility
{
    /// ------------------------------------------------------------
    /// Validation
    /// ------------------------------------------------------------

    /// @brief Validates all portable tree, text, geometry, relation, and limit invariants.
    /// @param snapshot Borrowed complete tree; all nested views must remain readable during the call.
    /// @param limits Positive capacities, all at most INT32_MAX; extension depth is at most 32.
    /// @return Validation details, retained on semantic failures; allocation failures have no payload.
    /// @note May allocate bounded temporary indexes; never calls a native backend or application callback.
    [[nodiscard]] DESKTOP_EXPORT Types::Accessibility::ValidationResult validate(
        const Types::Accessibility::SnapshotView &snapshot,
        const Types::Accessibility::Limits &limits = {}) noexcept;

    /// ------------------------------------------------------------
    /// Snapshot authoring
    /// ------------------------------------------------------------

    /// @brief Owning snapshot authoring and deep-copy container.
    /// @details Externally synchronized. Moved-from builders remain reusable. Construction allocates nothing;
    /// checked mutations may allocate. addNode() enforces copy budgets and unique IDs; validate()/publish()
    /// enforces complete semantic correctness, permitting forward references while authoring.
    // NOLINTNEXTLINE(performance-trivially-destructible) -- The incomplete-state pimpl requires an out-of-line destructor.
    class DESKTOP_EXPORT SnapshotBuilder final
    {
    public:
        /// @name Lifecycle
        /// @{

        SnapshotBuilder() noexcept; ///< Creates an empty, allocation-free builder with default limits.
        explicit SnapshotBuilder(
            const Types::Accessibility::Limits &limits) noexcept; ///< Creates an empty builder with a copy of the supplied limits.
        ~SnapshotBuilder() noexcept;                              ///< Releases all owned snapshot data.

        SnapshotBuilder(const SnapshotBuilder &) = delete; ///< Owning builders cannot be copied; use copySnapshot() for an explicit deep copy.
        SnapshotBuilder &operator=(const SnapshotBuilder &) = delete;
        SnapshotBuilder(SnapshotBuilder &&) noexcept;            ///< Transfers owned authoring storage without allocating.
        SnapshotBuilder &operator=(SnapshotBuilder &&) noexcept; ///< Replaces authoring storage by moving without allocation.

        /// @}

        /// @name Authoring
        /// @{

        /// @brief Removes all owned nodes and resets generation and root.
        void clear() noexcept;

        /// @brief Returns the limits used when adding or copying data.
        [[nodiscard]] const Types::Accessibility::Limits &limits() const noexcept;

        /// @brief Sets the nonzero generation copied into the next view.
        [[nodiscard]] GameWIP::Status::Types::Result<void> setGeneration(Types::Accessibility::Generation generation) noexcept;

        /// @brief Sets the application root node identity.
        [[nodiscard]] GameWIP::Status::Types::Result<void> setRoot(Types::Accessibility::NodeId root) noexcept;

        /// @brief Deep-copies one borrowed node and all of its referenced data.
        /// @return InvalidArgument for invalid limits/zero or duplicate IDs; SizeLimitExceeded for copy-budget pressure;
        /// OutOfMemory for allocation failure. No node is appended on failure.
        [[nodiscard]] GameWIP::Status::Types::Result<void> addNode(const Types::Accessibility::Node &node) noexcept;
        /// @}

        /// @name Inspection
        /// @{

        /// @brief Returns the number of owned nodes.
        [[nodiscard]] std::size_t nodeCount() const noexcept;

        /// @brief Returns a borrowed view into the builder's owned storage.
        /// @details The view remains valid until the builder is mutated or destroyed.
        [[nodiscard]] Types::Accessibility::SnapshotView view() const noexcept;
        /// @}

    private:
        std::unique_ptr<Internal::SnapshotBuilderState> state_;
        Types::Accessibility::Limits limits_;
    };

    /// ------------------------------------------------------------
    /// Immutable snapshot inspection
    /// ------------------------------------------------------------

    /// @brief Read-only structural view of one published snapshot.
    /// @details Passive immutable retention. Independent reader copies may be inspected on any thread;
    /// replacing/destroying the same reader requires external synchronization. A reader survives publication,
    /// disable, Window close, and Window destruction. All returned views/pointers expire with the last retaining reader.
    class DESKTOP_EXPORT SnapshotReader final
    {
    public:
        /// @name Lifecycle
        /// @{

        SnapshotReader() noexcept = default; ///< Creates an empty, allocation-free reader.
        ~SnapshotReader() noexcept;          ///< Releases immutable retention, possibly reclaiming snapshot storage.

        SnapshotReader(const SnapshotReader &) noexcept = default;            ///< Shares immutable retention without allocating.
        SnapshotReader &operator=(const SnapshotReader &) noexcept = default; ///< Replaces immutable retention by sharing the source snapshot.
        SnapshotReader(SnapshotReader &&) noexcept = default;                 ///< Transfers immutable retention without allocating.
        SnapshotReader &operator=(SnapshotReader &&) noexcept = default;      ///< Replaces immutable retention by transferring the source snapshot.

        /// @}

        /// @name Inspection
        /// @{

        /// @brief Reports whether the reader retains a published snapshot.
        [[nodiscard]] bool isValid() const noexcept;

        /// @brief Returns metadata for the retained snapshot.
        [[nodiscard]] Types::Accessibility::SnapshotInfo info() const noexcept;

        /// @brief Returns a borrowed view retained by this reader.
        [[nodiscard]] Types::Accessibility::SnapshotView view() const noexcept;

        /// @brief Finds one node without allocation or native calls.
        /// @return Borrowed immutable node or nullptr; logarithmic in node count.
        [[nodiscard]] const Types::Accessibility::Node *find(Types::Accessibility::NodeId node) const noexcept;
        /// @}

    private:
        friend class Facade;
        explicit SnapshotReader(std::shared_ptr<const Internal::PublishedSnapshot> snapshot) noexcept;
        std::shared_ptr<const Internal::PublishedSnapshot> snapshot_;
    };
} // namespace GameWIP::Desktop::Accessibility
