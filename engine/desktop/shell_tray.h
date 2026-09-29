/// @file shell_tray.h
/// @brief Portable system-tray icon and menu integration for GameWIP Desktop.

#pragma once

#include "desktop/desktop_export.h"
#include "desktop/shell.h"
#include "desktop/shell_types.h"
#include "desktop/window.h"
#include "io/status.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>

namespace GameWIP::Desktop
{
    namespace Detail
    {
        struct TrayIconAccess;
        struct TrayIconState;
    } // namespace Detail
} // namespace GameWIP::Desktop

namespace GameWIP::Desktop::Types::Tray
{
    /// @brief Portable kind of one tray-menu entry.
    enum class MenuItemKind : std::uint8_t
    {
        Command,   ///< Invokes a caller-defined command identity.
        Check,     ///< Invokes a caller-defined command and displays a check state.
        Radio,     ///< Invokes a caller-defined command and displays a radio state.
        Separator, ///< Visual separator without a command identity.
        Submenu    ///< Opens a nested menu without a command identity.
    };

    /// @brief Borrowed recursive tray-menu entry.
    /// @details All referenced strings, icons, and child spans are copied before the receiving
    /// open or replacement operation returns. Command, Check, and Radio entries require a
    /// unique nonzero command identity, a nonempty label, and empty children. Submenu entries
    /// require an invalid command identity, a nonempty label, nonempty children, and an empty
    /// accelerator. Separator entries require an invalid command identity and empty label,
    /// icon, accelerator, badge, and children. The checked state is meaningful only for Check
    /// and Radio entries.
    struct MenuItem
    {
        MenuItemKind kind = MenuItemKind::Command; ///< Entry presentation and interaction kind.
        Shell::CommandId commandId;                ///< Required command identity for actionable entries.
        std::string_view label;                    ///< Nonempty UTF-8 label except for separators.
        std::optional<IconImageView> icon;         ///< Optional borrowed RGBA8 icon for non-separators.
        std::string_view accelerator;              ///< Optional UTF-8 accelerator display text for actionable entries.
        std::string_view badge;                    ///< Optional UTF-8 badge or glyph display text for non-separators.
        std::span<const MenuItem> children;        ///< Nonempty child entries only for Submenu.
        bool enabled = true;                       ///< Whether a non-separator entry accepts user interaction; true for separators.
        bool checked = false;                      ///< Check or radio state; false for other entry kinds.
    };

    /// @brief Borrowed complete tray-menu tree.
    /// @details An empty item span clears the menu. Nonempty menus require a bound
    /// ShellEventQueue so command invocations have a delivery destination. Command identities
    /// must be unique across the complete recursive tree.
    struct MenuDescription
    {
        std::span<const MenuItem> items; ///< Borrowed root-level menu entries.
    };

    /// @brief Initial tray-icon presentation and menu snapshot.
    /// @details Icon views, tooltip text, and the complete recursive menu tree are copied before
    /// open returns. The icon span must be nonempty. A nonempty menu requires the queue overload.
    struct Description
    {
        std::span<const IconImageView> icons; ///< Nonempty borrowed RGBA8 icon variants.
        std::string_view tooltip;             ///< Optional UTF-8 tooltip; empty disables the tooltip.
        MenuDescription menu;                 ///< Borrowed initial menu tree.
    };
} // namespace GameWIP::Desktop::Types::Tray

namespace GameWIP::Desktop
{
    /// @brief Non-copyable, non-movable owner of one process-local tray icon binding.
    /// @details A successful open binds this icon to its owner thread and generates one
    /// process-local TrayIconId. Opening without a queue provides passive icon and tooltip
    /// presentation; user interactions are discarded until a queue is bound. A queue may be
    /// bound once before the first nonempty menu is published. Tray resources publish no
    /// callbacks and retain no state in Window.
    class DESKTOP_EXPORT TrayIcon final
    {
    public:
        /// @name Lifecycle
        /// @{

        /// @brief Constructs a closed tray-icon binding.
        TrayIcon() noexcept;
        TrayIcon(const TrayIcon &) = delete;
        TrayIcon &operator=(const TrayIcon &) = delete;
        TrayIcon(TrayIcon &&) = delete;
        TrayIcon &operator=(TrayIcon &&) = delete;

        /// @brief Best-effort closes the tray-icon binding.
        /// @details The caller must release the binding on its owner thread before destruction.
        ~TrayIcon() noexcept;

        /// @brief Opens passive icon and tooltip presentation without event delivery.
        /// @details The initial menu must be empty. A nonempty menu requires the queue overload.
        /// @param description Initial icon, tooltip, and empty-menu presentation snapshot.
        /// @return Success, or a status explaining why no tray icon was opened.
        [[nodiscard]] IO::Types::Status open(const Types::Tray::Description &description) noexcept;

        /// @brief Opens icon presentation with interaction events delivered through a queue.
        /// @param description Initial icon, tooltip, and menu presentation snapshot.
        /// @param eventQueue Open queue owned by the calling thread; borrowed until close.
        /// @return Success, or a status explaining why no tray icon was opened.
        [[nodiscard]] IO::Types::Status open(const Types::Tray::Description &description, ShellEventQueue &eventQueue) noexcept;

        /// @brief Reports whether the tray-icon binding is open.
        [[nodiscard]] bool isOpen() const noexcept;

        /// @brief Returns the generated process-local tray-icon identity.
        /// @return Tray-icon identity, or an invalid identity while closed.
        [[nodiscard]] Types::Shell::TrayIconId id() const noexcept;

        /// @brief Reports whether the calling thread owns the current binding lifetime.
        [[nodiscard]] bool ownedByCurrentThread() const noexcept;

        /// @brief Reports whether interaction events have a bound queue.
        [[nodiscard]] bool hasEventQueue() const noexcept;

        /// @brief Binds an event queue once before the first nonempty menu is published.
        /// @details The tray icon must be open without a queue, the queue must be open on the
        /// calling thread, and no nonempty menu may have been published. The queue is borrowed
        /// until close and cannot be replaced or detached.
        /// @param eventQueue Open queue owned by the calling thread.
        /// @return Success, NotOpen, InvalidArgument for a prohibited binding, ResourceBusy for
        /// wrong-thread access, or another queue/native failure.
        [[nodiscard]] IO::Types::Status bindEventQueue(ShellEventQueue &eventQueue) noexcept;

        /// @brief Closes the binding and releases its tray publication.
        /// @return Success when closed or already closed; ResourceBusy for wrong-thread calls or retained native work.
        [[nodiscard]] IO::Types::Status close() noexcept;

        /// @}

        /// @name Presentation updates
        /// @{

        /// @brief Replaces the tray icon image variants.
        /// @param icons Nonempty borrowed RGBA8 icon views copied before return.
        /// @return Success, or a validation, ownership, capability, allocation, or native failure.
        [[nodiscard]] IO::Types::Status setIcon(std::span<const Types::IconImageView> icons) noexcept;

        /// @brief Replaces or clears the tray tooltip.
        /// @param tooltip Valid UTF-8 tooltip text without embedded U+0000; empty clears the tooltip.
        /// @return Success, or a validation, ownership, capability, allocation, or native failure.
        [[nodiscard]] IO::Types::Status setTooltip(std::string_view tooltip) noexcept;

        /// @brief Replaces the complete recursive tray-menu tree.
        /// @details An empty menu clears the current menu. A nonempty menu requires a bound event
        /// queue. A failed replacement leaves the previously published menu unchanged.
        /// @param menu Borrowed complete menu tree copied before return.
        /// @return Success, or a validation, ownership, queue, capability, size, allocation, or native failure.
        [[nodiscard]] IO::Types::Status setMenu(const Types::Tray::MenuDescription &menu) noexcept;

        /// @}

    private:
        friend struct Detail::TrayIconAccess;

        std::unique_ptr<Detail::TrayIconState> state_;
    };
} // namespace GameWIP::Desktop
