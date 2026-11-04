/// @file system.h
/// @brief Stateful portable input system. Hardware backends are implementation details.
#pragma once

#include "input/frame.h"
#include "io/status.h"

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

#include <memory>
#include <span>

namespace GameWIP::Input::Types
{
    /// @brief Result of publishing one coherent frame.
    struct CaptureResult
    {
        IO::Types::Status status{};        ///< Operation status.
        bool published = false;            ///< Whether destination contains a new frame.
        std::uint64_t captureSequence = 0; ///< Sequence of the published frame.
        std::uint64_t droppedEvents = 0;   ///< Events lost before the published frame.
        bool discontinuity = false;        ///< Whether the published interval was discontinuous.
    };
} // namespace GameWIP::Input::Types

namespace GameWIP::Input
{
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
    class INPUT_EXPORT System final
    {
    public:
        /// @brief Creates a closed input system.
        System() noexcept;

        /// @brief Closes the system as a best-effort cleanup operation.
        /// @details The destructor does not report a close failure. Call close explicitly when the status matters.
        ~System() noexcept;

        System(const System &) = delete;

        System &operator=(const System &) = delete;

        System(System &&) = delete;

        System &operator=(System &&) = delete;

        /// @brief Opens an owner-thread-affine capture session.
        /// @param configuration Open-time behavior and bounded queue capacities.
        /// @return Success, AlreadyOpen, ResourceBusy, InvalidArgument, or OutOfMemory.
        [[nodiscard]] IO::Types::Status open(const Types::Configuration &configuration = {}) noexcept;

        /// @brief Closes the current capture session.
        /// @return Success, ResourceBusy, or OutOfMemory.
        [[nodiscard]] IO::Types::Status close() noexcept;

        /// @brief Reports whether a capture session is open.
        /// @return `true` after a successful open and before close.
        [[nodiscard]] bool isOpen() const noexcept;

        /// @brief Reports whether the current thread owns the open session.
        /// @return `true` only for the thread that opened the system.
        [[nodiscard]] bool ownedByCurrentThread() const noexcept;

        /// @brief Publishes a coherent state and event snapshot.
        /// @param destination Caller-owned frame replaced only after a successful capture.
        /// @return Capture status and interval loss information.
        /// @details The operation clears interval events and transition counters
        /// only after the destination has been published successfully. On
        /// failure, destination remains unchanged.
        [[nodiscard]] Types::CaptureResult capture(Types::Frame &destination) noexcept;

        /// @brief Borrows all device descriptors known during the current open session.
        /// @param outDevices Receives a read-only span owned by the System.
        /// @return Success, NotOpen, ResourceBusy, or OutOfMemory.
        /// @note The span is invalidated by the next mutating System or producer operation.
        [[nodiscard]] IO::Types::Status devices(std::span<const Types::DeviceInfo> &outDevices) const noexcept;

        /// @brief Borrows one known device descriptor.
        /// @param id Runtime device identity to find.
        /// @param outInfo Receives a pointer owned by the System, or null when the device is absent.
        /// @return Success, InvalidArgument, NotFound, NotOpen, ResourceBusy, or OutOfMemory.
        /// @note The pointer is invalidated by the next mutating System or producer operation.
        [[nodiscard]] IO::Types::Status findDevice(Types::DeviceId id, const Types::DeviceInfo *&outInfo) const noexcept;

        /// @brief Borrows one element descriptor.
        /// @param id Runtime element identity to find.
        /// @param outInfo Receives a pointer owned by the System, or null when the element is absent.
        /// @return Success, InvalidArgument, NotFound, NotOpen, ResourceBusy, or OutOfMemory.
        /// @note The pointer is invalidated by the next mutating System or producer operation.
        [[nodiscard]] IO::Types::Status findElement(Types::ElementId id, const Types::ElementInfo *&outInfo) const noexcept;

        /// @brief Copies health counters accumulated during the current open session.
        /// @param outHealth Receives a value copy of health counters.
        /// @return Success, NotOpen, ResourceBusy, or OutOfMemory.
        [[nodiscard]] IO::Types::Status health(Types::Health &outHealth) const noexcept;

        /// @brief Copies one device descriptor into caller-owned storage.
        /// @param id Runtime device identity to copy.
        /// @param outInfo Receives an independent descriptor copy.
        /// @return Success, InvalidArgument, NotFound, NotOpen, ResourceBusy, or OutOfMemory.
        [[nodiscard]] IO::Types::Status copyDeviceInfo(Types::DeviceId id, Types::DeviceInfo &outInfo) const noexcept;

        /// @brief Enables or disables future text events for the session.
        /// @param enabled Whether text events should be accepted.
        /// @return Success, NotOpen, ResourceBusy, or OutOfMemory.
        [[nodiscard]] IO::Types::Status setTextEnabled(bool enabled) noexcept;

        /// @brief Changes the maximum UTF-8 text retained before the next capture.
        /// @param maximumBytes Positive maximum pending UTF-8 byte count.
        /// @return Success, InvalidArgument, SizeLimitExceeded, NotOpen, ResourceBusy, or OutOfMemory.
        /// @note Reducing the limit below currently pending text is rejected without changing the limit.
        [[nodiscard]] IO::Types::Status setMaximumPendingTextBytes(std::size_t maximumBytes) noexcept;

    public:
        /// @brief Opaque implementation declaration used by in-tree producer attachments.
        struct Impl;

    private:
        friend class Synthetic::Source;
        friend class Platform::Win32::Attachment;

        std::unique_ptr<Impl> impl_{};
    };
} // namespace GameWIP::Input
