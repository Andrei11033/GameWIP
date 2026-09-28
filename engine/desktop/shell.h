/// @file shell.h
/// @brief Portable top-level desktop-shell API for GameWIP.

#pragma once

#include "desktop/desktop_export.h"
#include "desktop/events.h"
#include "desktop/shell_types.h"
#include "io/status.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace GameWIP::Desktop
{
    namespace Detail
    {
        struct ShellEventQueueState;
        struct ShellEventQueueAccess;
    } // namespace Detail
} // namespace GameWIP::Desktop

namespace GameWIP::Desktop::Types::Shell
{
    /// @brief Backend capabilities exposed by Desktop shell integration.
    enum class Capability : std::uint8_t
    {
        Taskbar,                      ///< Basic taskbar integration is supported.
        TaskbarProgress,              ///< Taskbar progress state is supported.
        TaskbarOverlayIcon,           ///< Taskbar overlay icons are supported.
        TaskbarThumbnailButtons,      ///< Taskbar thumbnail buttons are supported.
        TrayIcon,                     ///< System-tray icons are supported.
        TrayMenu,                     ///< Tray menus are supported.
        TrayMenuIcons,                ///< Tray menu icons are supported.
        TrayMenuAccelerators,         ///< Tray menu accelerator labels or shortcuts are supported.
        TrayMenuBadges,               ///< Tray menu badge or glyph indicators are supported.
        Notifications,                ///< Basic notifications are supported.
        NotificationActions,          ///< Notification actions are supported.
        NotificationInputs,           ///< Notification text-input fields are supported.
        NotificationMedia,            ///< Notification media is supported.
        NotificationScheduling,       ///< Notification scheduling and recurrence are supported.
        NotificationGrouping,         ///< Notification grouping is supported.
        NotificationSound,            ///< Notification sound policy is supported.
        NotificationUrgency,          ///< Notification urgency policy is supported.
        NotificationBadges,           ///< Notification badges are supported.
        NotificationProgress,         ///< Notification progress is supported.
        JumpLists,                    ///< Jump-list publication is supported.
        RecentItems,                  ///< Recent-item publication is supported.
        FileAssociationRegistration,  ///< File-extension registration is supported.
        UriSchemeRegistration,        ///< URI-scheme registration is supported.
        RegistrationVerbs,            ///< Registration verbs are supported.
        RegistrationExtendedMetadata, ///< Extended registration metadata is supported.
        Count                         ///< Enumerator count used to bound capability indexes.
    };

    /// @brief Backend capability flags for Desktop shell integration.
    struct Capabilities
    {
        std::uint64_t flags = 0; ///< Bit set indexed by Capability.

        /// @brief Reports whether the backend advertises the requested capability.
        [[nodiscard]] constexpr bool supports(Capability capability) const noexcept
        {
            const auto index = static_cast<std::uint8_t>(capability);
            return index < static_cast<std::uint8_t>(Capability::Count) && (flags & (std::uint64_t{1} << index)) != 0;
        }
    };

    /// @brief Status and cached backend shell capability snapshot.
    struct CapabilitiesResult
    {
        IO::Types::Status status;  ///< Capability-query status.
        Capabilities capabilities; ///< Cached capability values on success.
    };
} // namespace GameWIP::Desktop::Types::Shell

namespace GameWIP::Desktop
{
    /// @brief Non-copyable, non-movable owner of one typed Desktop shell event queue.
    /// @details A successful open binds the queue to the opening thread. Lifecycle operations,
    /// event consumption, and queue metadata/mutation require that owner thread while open.
    /// Shell resources may publish events from other threads. The queue must outlive every
    /// resource bound to it.
    class DESKTOP_EXPORT ShellEventQueue final
    {
    public:
        /// @name Lifecycle
        /// @{

        /// @brief Constructs a closed shell event queue.
        ShellEventQueue() noexcept;
        ShellEventQueue(const ShellEventQueue &) = delete;
        ShellEventQueue &operator=(const ShellEventQueue &) = delete;
        ShellEventQueue(ShellEventQueue &&) = delete;
        ShellEventQueue &operator=(ShellEventQueue &&) = delete;

        /// @brief Best-effort closes the queue and releases its storage.
        /// @details The caller must release every bound shell resource before destruction.
        ~ShellEventQueue() noexcept;

        /// @brief Opens with Events::kDefaultQueueCapacity internally owned event slots.
        /// @return Success, or a status explaining why the queue was not opened.
        [[nodiscard]] IO::Types::Status open() noexcept;

        /// @brief Opens with an internally owned fixed event capacity.
        /// @param capacity Number of event slots; must be greater than zero.
        /// @return Success, or a status explaining why the queue was not opened.
        [[nodiscard]] IO::Types::Status open(std::size_t capacity) noexcept;

        /// @brief Opens while borrowing caller-owned event storage until close.
        /// @param eventStorage Non-empty storage that must remain alive and unmoved until close.
        /// @return Success, or a status explaining why the queue was not opened.
        [[nodiscard]] IO::Types::Status open(std::span<Types::Shell::Event> eventStorage) noexcept;

        /// @brief Reports whether the queue is open for event publication and consumption.
        [[nodiscard]] bool isOpen() const noexcept;

        /// @brief Reports whether the calling thread owns the current open queue lifetime.
        [[nodiscard]] bool ownedByCurrentThread() const noexcept;

        /// @brief Closes the queue and releases or stops borrowing its event storage.
        /// @return Success when closed; ResourceBusy for wrong-thread calls or bound resources.
        [[nodiscard]] IO::Types::Status close() noexcept;

        /// @}

        /// @name Event consumption
        /// @{

        /// @brief Removes the oldest queued event on the owner thread.
        /// @param outEvent Destination replaced with the oldest event when available.
        /// @return True when an event was removed; otherwise false.
        [[nodiscard]] bool popEvent(Types::Shell::Event &outEvent) noexcept;

        /// @brief Removes up to destination.size() queued events in FIFO order.
        /// @param destination Caller-owned output slots.
        /// @return Number of events removed.
        [[nodiscard]] std::size_t popEvents(std::span<Types::Shell::Event> destination) noexcept;

        /// @brief Discards all queued events on the owner thread.
        void clearEvents() noexcept;

        /// @brief Returns cached storage kind, capacity, pending-event count, and drop counter.
        /// @details Returns default values while closed.
        [[nodiscard]] Types::Events::QueueInfo eventQueueInfo() const noexcept;

        /// @brief Resets the cumulative dropped-event counter on the owner thread.
        void clearDroppedEventCount() noexcept;

        /// @}

    private:
        friend struct Detail::ShellEventQueueAccess;

        std::unique_ptr<Detail::ShellEventQueueState> state_;
    };
} // namespace GameWIP::Desktop

namespace GameWIP::Desktop::Shell
{

    /// @brief Returns cached backend and environment shell capabilities.
    [[nodiscard]] DESKTOP_EXPORT Types::Shell::CapabilitiesResult getCapabilities() noexcept;

    /// @brief Reports whether the backend advertises the requested shell capability.
    [[nodiscard]] DESKTOP_EXPORT bool supports(Types::Shell::Capability capability) noexcept;
} // namespace GameWIP::Desktop::Shell
