/// @file system.h
/// @brief Stateful portable input system. Hardware backends are implementation details.
#pragma once

#include "input/frame.h"
#include "status/result.h"

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

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace GameWIP::Input::Types
{
    /// @brief Metadata returned after publishing one coherent frame.
    struct CaptureInfo
    {
        std::uint64_t captureSequence = 0; ///< Sequence of the published frame.
        std::uint64_t droppedEvents = 0;   ///< Events lost before the published frame.
        bool discontinuity = false;        ///< Whether the published interval was discontinuous.
    };

    /// @brief Status and metadata returned by one frame capture.
    using CaptureResult = GameWIP::Status::Types::Result<CaptureInfo>;
} // namespace GameWIP::Input::Types

namespace GameWIP::Input
{
    namespace Internal
    {
        struct SystemState;
    }

    namespace Platform::Win32
    {
        class Attachment;
    }

    namespace Synthetic
    {
        class Source;
    }

    /// @brief Owner-thread-affine input acquisition owner.
    /// @details A System starts closed. The thread that successfully opens it
    /// owns all mutable operations and borrowed metadata views until close.
    /// The portable core does not create a worker thread.
    class System final
    {
    public:
        // ------------------------------------------------------------
        // Session lifecycle
        // ------------------------------------------------------------

        /// @brief Creates a closed input system.
        INPUT_EXPORT System() noexcept;

        /// @brief Closes the system as a best-effort cleanup operation.
        /// @details The destructor does not report a close failure. Call close explicitly when the status matters.
        INPUT_EXPORT ~System() noexcept;

        System(const System &) = delete;

        System &operator=(const System &) = delete;

        System(System &&) = delete;

        System &operator=(System &&) = delete;

        /// @brief Opens an owner-thread-affine capture session.
        /// @param configuration Open-time behavior and bounded queue capacities.
        /// @return Operation status.
        [[nodiscard]] INPUT_EXPORT GameWIP::Status::Types::Result<void> open(const Types::Configuration &configuration = {}) noexcept;

        /// @brief Closes the current capture session.
        /// @return Operation status.
        [[nodiscard]] INPUT_EXPORT GameWIP::Status::Types::Result<void> close() noexcept;

        /// @brief Reports whether a capture session is open.
        /// @return `true` after a successful open and before close.
        [[nodiscard]] INPUT_EXPORT bool isOpen() const noexcept;

        /// @brief Reports whether the current thread owns the open session.
        /// @return `true` only for the thread that opened the system.
        [[nodiscard]] INPUT_EXPORT bool ownedByCurrentThread() const noexcept;

        // ------------------------------------------------------------
        // Frame capture
        // ------------------------------------------------------------

        /// @brief Publishes a coherent state and event snapshot.
        /// @param destination Caller-owned frame replaced only after a successful capture.
        /// @return Capture metadata on success.
        /// @details The operation clears interval events and transition counters
        /// only after the destination has been published successfully. On
        /// failure, destination remains unchanged.
        [[nodiscard]] INPUT_EXPORT Types::CaptureResult capture(Types::Frame &destination) noexcept;

        // ------------------------------------------------------------
        // Device descriptors and health
        // ------------------------------------------------------------

        /// @brief Borrows all device descriptors known during the current open session.
        /// @return Read-only span owned by the System.
        /// @note The span is invalidated by the next mutating System or producer operation.
        [[nodiscard]] INPUT_EXPORT GameWIP::Status::Types::Result<std::span<const Types::DeviceInfo>> devices() const noexcept;

        /// @brief Borrows one known device descriptor.
        /// @param id Runtime device identity to find.
        /// @return Borrowed descriptor, or NotFound when absent.
        /// @note The pointer is invalidated by the next mutating System or producer operation.
        [[nodiscard]] INPUT_EXPORT GameWIP::Status::Types::Result<const Types::DeviceInfo *> findDevice(Types::DeviceId id) const noexcept;

        /// @brief Borrows one element descriptor.
        /// @param id Runtime element identity to find.
        /// @return Borrowed descriptor, or NotFound when absent.
        /// @note The pointer is invalidated by the next mutating System or producer operation.
        [[nodiscard]] INPUT_EXPORT GameWIP::Status::Types::Result<const Types::ElementInfo *> findElement(Types::ElementId id) const noexcept;

        /// @brief Copies health counters accumulated during the current open session.
        /// @return A copy of health counters.
        [[nodiscard]] INPUT_EXPORT GameWIP::Status::Types::Result<Types::Health> health() const noexcept;

        /// @brief Copies one device descriptor into caller-owned storage.
        /// @param id Runtime device identity to copy.
        /// @return An independent descriptor copy.
        [[nodiscard]] INPUT_EXPORT GameWIP::Status::Types::Result<Types::DeviceInfo> copyDeviceInfo(Types::DeviceId id) const noexcept;

        // ------------------------------------------------------------
        // Text input
        // ------------------------------------------------------------

        /// @brief Enables or disables future text events for the session.
        /// @param enabled Whether text events should be accepted.
        /// @return Operation status.
        [[nodiscard]] INPUT_EXPORT GameWIP::Status::Types::Result<void> setTextEnabled(bool enabled) noexcept;

        /// @brief Changes the maximum UTF-8 text retained before the next capture.
        /// @param maximumBytes Positive maximum pending UTF-8 byte count.
        /// @return Operation status.
        /// @note Reducing the limit below currently pending text is rejected without changing the limit.
        [[nodiscard]] INPUT_EXPORT GameWIP::Status::Types::Result<void> setMaximumPendingTextBytes(std::size_t maximumBytes) noexcept;

    private:
        friend class Synthetic::Source;
        friend class Platform::Win32::Attachment;

        std::unique_ptr<Internal::SystemState> impl_{};
    };
} // namespace GameWIP::Input
