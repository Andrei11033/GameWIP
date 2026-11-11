/// @file system_state.h
/// @brief Private state shared by the portable Input implementation.
#pragma once

#include "input/system.h"

#include <memory>
#include <thread>
#include <utility>

namespace GameWIP::Input
{
    /// @brief Private storage for one owner-thread-affine System instance.
    struct System::Impl
    {
        /// @brief Callback used by an attached native source before a frame is copied.
        using NativeCaptureHook = void (*)(Impl &, void *) noexcept;

        /// @brief Thread that opened the system and owns its mutable operations.
        std::thread::id ownerThread{};
        /// @brief Whether the system currently has an open capture session.
        bool open = false;
        /// @brief Configuration copied at the last successful open operation.
        Types::Configuration configuration{};
        /// @brief Session token allowing producers to detect System destruction safely.
        std::shared_ptr<void> sessionLifetime{};
        /// @brief Runtime identity shared by all IDs from this open session.
        std::uint64_t instance = 0;
        /// @brief Next runtime device slot to allocate.
        std::uint32_t nextDeviceSlot = 1;
        /// @brief Next runtime element slot to allocate.
        std::uint32_t nextElementSlot = 1;
        /// @brief Sequence assigned to the next published frame or event.
        std::uint64_t nextSequence = 1;
        /// @brief Monotonic revision for descriptor changes.
        std::uint64_t deviceRevision = 0;
        /// @brief Whether text events are accepted for the current session.
        bool textEnabled = true;

        /// @brief Native source hook invoked before a capture copy.
        NativeCaptureHook nativeBeforeCapture = nullptr;
        /// @brief Native source hook invoked after a successful capture copy.
        NativeCaptureHook nativeAfterCapture = nullptr;
        /// @brief Native source cleanup hook invoked while closing the session.
        NativeCaptureHook nativeClose = nullptr;
        /// @brief Opaque native source context passed to native hooks.
        void *nativeContext = nullptr;

        /// @brief Current device descriptors owned by this system.
        std::vector<Types::DeviceInfo> devices{};
        /// @brief Latest state for every element seen during this session.
        std::vector<Types::ElementState> states{};
        /// @brief Events accumulated since the previous successful capture.
        std::vector<Types::Event> pendingEvents{};
        /// @brief Raw reports accumulated since the previous successful capture.
        std::vector<Types::RawReport> pendingRawReports{};
        /// @brief Text accumulated since the previous successful capture.
        std::string pendingTextUtf8{};
        /// @brief Number of events discarded since the previous successful capture.
        std::uint64_t pendingDroppedEvents = 0;
        /// @brief Whether this interval contains an observable discontinuity.
        bool pendingDiscontinuity = false;
        /// @brief Health counters for the current open session.
        Types::Health health{};

        /// @brief Appends an event or records a bounded-queue drop.
        /// @param event Event to append.
        void appendEvent(Types::Event event) noexcept;

        /// @brief Finds a device descriptor by runtime ID.
        /// @param id Runtime device ID to find.
        /// @return Device descriptor index, or `devices.size()` when absent.
        [[nodiscard]] std::size_t findDeviceIndex(Types::DeviceId id) const noexcept;

        /// @brief Finds an element descriptor by runtime ID.
        /// @param id Runtime element ID to find.
        /// @return Pair of device and element indices, or invalid indices when absent.
        [[nodiscard]] std::pair<std::size_t, std::size_t> findElementIndex(Types::ElementId id) const noexcept;

        /// @brief Finds the latest state by runtime element ID.
        /// @param id Runtime element ID to find.
        /// @return State index, or `states.size()` when absent.
        /// @details Element slots are allocated monotonically and states are
        /// retained in that same slot order for the open session, allowing
        /// direct lookup without a per-element hash table.
        [[nodiscard]] std::size_t findStateIndex(Types::ElementId id) const noexcept;

        /// @brief Finds a device descriptor and element descriptor by runtime ID.
        /// @param id Runtime element ID to find.
        /// @return Mutable element descriptor, or null when absent.
        [[nodiscard]] Types::ElementInfo *findElement(Types::ElementId id) noexcept;

        /// @brief Finds a device descriptor and element descriptor by runtime ID.
        /// @param id Runtime element ID to find.
        /// @return Immutable element descriptor, or null when absent.
        [[nodiscard]] const Types::ElementInfo *findElement(Types::ElementId id) const noexcept;
    };
} // namespace GameWIP::Input
