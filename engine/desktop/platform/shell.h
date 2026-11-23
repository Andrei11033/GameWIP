/// @file shell.h
/// @brief Portable backend contract for Desktop shell integration.

#pragma once

#include "desktop/internal/shell_state.h"
#include "desktop/shell.h"

#include <cstdint>
#include <string_view>

namespace GameWIP::Desktop::Platform
{
    /// ------------------------------------------------------------
    /// Shared portable state
    /// ------------------------------------------------------------

    using Internal::NotificationCenterState;
    using Internal::OwnedNotificationAction;
    using Internal::OwnedNotificationDescription;
    using Internal::OwnedNotificationInput;
    using Internal::OwnedNotificationMedia;
    using Internal::OwnedThumbnailButton;
    using Internal::OwnedTrayMenuItem;
    using Internal::TaskbarItemState;
    using Internal::TrayIconState;
    using Internal::WindowState;

    /// @brief Returns shell capabilities available on the current host.
    [[nodiscard]] Types::Result<Types::Shell::Capabilities> getShellCapabilities() noexcept;

    /// @brief Reports whether the calling thread owns shell event sources.
    /// @details Keeps the shared event pump active without requiring a top-level Window.
    [[nodiscard]] bool hasShellEventSources() noexcept;

    /// ------------------------------------------------------------
    /// Taskbar
    /// ------------------------------------------------------------

    /// @brief Creates the taskbar integration for one Window.
    [[nodiscard]] IO::Types::Status openTaskbar(TaskbarItemState &state) noexcept;
    /// @brief Applies cached taskbar state to the native shell.
    [[nodiscard]] IO::Types::Status applyTaskbar(TaskbarItemState &state) noexcept;
    /// @brief Closes taskbar integration.
    [[nodiscard]] IO::Types::Status closeTaskbar(TaskbarItemState &state) noexcept;
    /// @brief Attempts taskbar cleanup during teardown.
    void closeTaskbarBestEffort(TaskbarItemState &state) noexcept;
    /// @brief Removes taskbar state after the associated native Window is destroyed.
    void notifyTaskbarWindowDestroyed(WindowState &window) noexcept;
    /// @brief Routes a taskbar thumbnail command into the shell event queue.
    void routeTaskbarCommand(WindowState &window, std::uint32_t commandId) noexcept;

    /// ------------------------------------------------------------
    /// Tray
    /// ------------------------------------------------------------

    /// @brief Creates a tray icon.
    [[nodiscard]] IO::Types::Status openTray(TrayIconState &state) noexcept;
    /// @brief Applies cached tray state to the native shell.
    [[nodiscard]] IO::Types::Status applyTray(TrayIconState &state) noexcept;
    /// @brief Closes tray integration.
    [[nodiscard]] IO::Types::Status closeTray(TrayIconState &state) noexcept;
    /// @brief Attempts tray cleanup during teardown.
    void closeTrayBestEffort(TrayIconState &state) noexcept;

    /// ------------------------------------------------------------
    /// Notifications
    /// ------------------------------------------------------------

    /// @brief Creates the notification center bridge.
    [[nodiscard]] IO::Types::Status openNotificationCenter(NotificationCenterState &state) noexcept;
    /// @brief Publishes one notification and records its portable description.
    [[nodiscard]] IO::Types::Status publishNotification(
        NotificationCenterState &state,
        std::uint64_t id,
        const OwnedNotificationDescription &description) noexcept;
    /// @brief Updates one previously published notification.
    [[nodiscard]] IO::Types::Status updateNotification(
        NotificationCenterState &state,
        std::uint64_t id,
        const OwnedNotificationDescription &description) noexcept;
    /// @brief Dismisses one notification by portable identifier.
    [[nodiscard]] IO::Types::Status dismissNotification(NotificationCenterState &state, std::uint64_t id) noexcept;
    /// @brief Closes notification-center integration.
    [[nodiscard]] IO::Types::Status closeNotificationCenter(NotificationCenterState &state) noexcept;
    /// @brief Attempts notification-center cleanup during teardown.
    void closeNotificationCenterBestEffort(NotificationCenterState &state) noexcept;

    /// ------------------------------------------------------------
    /// Jump lists and registration
    /// ------------------------------------------------------------

    /// @brief Publishes taskbar jump-list entries.
    [[nodiscard]] IO::Types::Status publishJumpLists(const Types::JumpLists::Description &description) noexcept;
    /// @brief Registers one file extension with the shell.
    [[nodiscard]] Types::Registration::Result registerFileExtension(const Types::Registration::FileExtensionDescription &description) noexcept;
    /// @brief Registers one URI scheme with the shell.
    [[nodiscard]] Types::Registration::Result registerUriScheme(const Types::Registration::UriSchemeDescription &description) noexcept;
    /// @brief Removes one file-extension registration owned by ownerKey.
    [[nodiscard]] Types::Registration::Result unregisterFileExtension(std::string_view ownerKey, std::string_view extension) noexcept;
    /// @brief Removes one URI-scheme registration owned by ownerKey.
    [[nodiscard]] Types::Registration::Result unregisterUriScheme(std::string_view ownerKey, std::string_view scheme) noexcept;
} // namespace GameWIP::Desktop::Platform
