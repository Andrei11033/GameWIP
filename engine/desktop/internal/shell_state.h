/// @file shell_state.h
/// @brief Private state and ownership helpers for Desktop shell resources.

#pragma once

#include "desktop/shell_jump_lists.h"
#include "desktop/shell_notifications.h"
#include "desktop/shell_registration.h"
#include "desktop/shell_taskbar.h"
#include "desktop/shell_tray.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace GameWIP::Desktop::Detail
{
    struct WindowState;

    /// @brief One process-local, fixed-capacity shell event queue.
    /// @details The owner thread consumes and mutates queue state. Native callbacks may append
    /// events from another thread, so the queue mutex protects both publication and consumption.
    struct ShellEventQueueState
    {
        mutable std::mutex mutex;
        std::thread::id ownerThread;
        std::vector<Types::Shell::Event> internalEvents;
        std::span<Types::Shell::Event> eventStorage; ///< Internal or caller-owned slots retained until queue close.
        Types::Events::StorageKind storageKind = Types::Events::StorageKind::Internal;
        std::size_t eventHead = 0;          ///< Physical index of the oldest queued event.
        std::size_t eventCount = 0;         ///< Number of queued events in FIFO order.
        std::uint64_t nextSequence = 1;     ///< Monotonic sequence assigned to new events.
        std::uint64_t droppedEvents = 0;    ///< Number of events discarded under queue pressure.
        std::size_t boundResourceCount = 0; ///< Number of shell resources publishing into this queue.
        bool open = false;                  ///< Whether the queue may accept new publications.
    };

    /// @brief Publishes one shell payload while preserving queue ownership and sequencing.
    void publishShellEvent(ShellEventQueueState &queue, Types::Shell::Events::Payload payload) noexcept;

    /// @brief Owned icon pixels retained for native shell publication.
    struct OwnedIconImage
    {
        Types::PixelSize size;
        std::vector<std::byte> rgba8;
    };

    /// @brief Owned taskbar thumbnail-button data.
    struct OwnedThumbnailButton
    {
        Types::Shell::CommandId id;
        std::string label;
        std::optional<OwnedIconImage> icon;
        bool enabled = true;
    };

    /// @brief Native taskbar state bound to one Window and shell event queue.
    struct TaskbarItemState
    {
        std::thread::id ownerThread;
        WindowState *window = nullptr;
        Types::WindowId windowId;
        ShellEventQueueState *eventQueue = nullptr;
        std::optional<Types::Shell::Progress> progress;
        std::optional<OwnedIconImage> overlayIcon;
        std::vector<OwnedThumbnailButton> thumbnailButtons;
        void *platform = nullptr;
        bool open = false;
    };

    /// @brief Owned recursive tray menu item data.
    struct OwnedTrayMenuItem
    {
        Types::Tray::MenuItemKind kind = Types::Tray::MenuItemKind::Command;
        Types::Shell::CommandId commandId;
        std::string label;
        std::optional<OwnedIconImage> icon;
        std::string accelerator;
        std::string badge;
        std::vector<OwnedTrayMenuItem> children;
        bool enabled = true;
        bool checked = false;
    };

    /// @brief Native tray state and cached menu publication data.
    struct TrayIconState
    {
        std::thread::id ownerThread;
        Types::Shell::TrayIconId id;
        ShellEventQueueState *eventQueue = nullptr;
        std::vector<OwnedIconImage> icons;
        std::string tooltip;
        std::vector<OwnedTrayMenuItem> menu;
        void *platform = nullptr;
        bool open = false;
        bool menuWasPublished = false;
    };

    /// @brief Owned notification action data.
    struct OwnedNotificationAction
    {
        Types::Shell::CommandId id;
        std::string label;
    };

    /// @brief Owned notification input data.
    struct OwnedNotificationInput
    {
        Types::Shell::NotificationInputId id;
        std::string placeholder;
        std::string initialValue;
        bool secure = false;
    };

    /// @brief Owned notification media, either a URI or RGBA pixels.
    struct OwnedNotificationMedia
    {
        bool uri = false;
        std::string uriText;
        Types::PixelSize size;
        std::vector<std::byte> rgba8;
    };

    /// @brief Deep-copied notification description retained for update operations.
    struct OwnedNotificationDescription
    {
        std::string title;
        std::string body;
        std::vector<OwnedNotificationAction> actions;
        std::vector<OwnedNotificationInput> inputs;
        std::vector<OwnedNotificationMedia> media;
        std::optional<Types::Notifications::Schedule> schedule;
        std::string group;
        Types::Notifications::Sound sound = Types::Notifications::Sound::Default;
        Types::Notifications::Urgency urgency = Types::Notifications::Urgency::Normal;
        std::string badge;
        std::optional<Types::Shell::Progress> progress;
    };

    /// @brief Native notification-center state and published descriptions.
    struct NotificationCenterState
    {
        std::thread::id ownerThread;
        ShellEventQueueState *eventQueue = nullptr;
        std::unordered_map<std::uint64_t, OwnedNotificationDescription> notifications;
        std::uint64_t nextId = 1;
        void *platform = nullptr;
        bool open = false;
    };

    /// @brief Private access bridge from the public ShellEventQueue wrapper.
    struct ShellEventQueueAccess
    {
        [[nodiscard]] static ShellEventQueueState *state(ShellEventQueue &queue) noexcept
        {
            return queue.state_.get();
        }

        [[nodiscard]] static const ShellEventQueueState *state(const ShellEventQueue &queue) noexcept
        {
            return queue.state_.get();
        }
    };

    /// @brief Private access bridge from the public TaskbarItem wrapper.
    struct TaskbarItemAccess
    {
        [[nodiscard]] static TaskbarItemState *state(TaskbarItem &item) noexcept
        {
            return item.state_.get();
        }
    };

    /// @brief Private access bridge from the public TrayIcon wrapper.
    struct TrayIconAccess
    {
        [[nodiscard]] static TrayIconState *state(TrayIcon &icon) noexcept
        {
            return icon.state_.get();
        }
    };

    /// @brief Private access bridge from the public NotificationCenter wrapper.
    struct NotificationCenterAccess
    {
        [[nodiscard]] static NotificationCenterState *state(NotificationCenter &center) noexcept
        {
            return center.state_.get();
        }
    };
} // namespace GameWIP::Desktop::Detail
