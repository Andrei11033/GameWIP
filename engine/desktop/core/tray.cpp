/// @file tray.cpp
/// @brief Portable TrayIcon lifecycle and state forwarding.

#include "desktop/internal/results.h"
#include "desktop/shell_tray.h"

#include "desktop/internal/shell_helpers.h"
#include "desktop/internal/shell_state.h"
#include "desktop/platform/shell.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

namespace GameWIP::Desktop::Internal
{
    /// ------------------------------------------------------------
    /// Tray description helpers
    /// ------------------------------------------------------------

    namespace
    {
        using ErrorCode = GameWIP::Status::Types::Code;

        [[nodiscard]] GameWIP::Status::Types::Status error(ErrorCode code) noexcept
        {
            return GameWIP::Desktop::Internal::statusFor(code);
        }

        std::atomic_uint64_t nextShellIdentityValue{1};

        /// @brief Allocates a nonzero process-local shell identity, skipping wraparound zero.
        [[nodiscard]] std::uint64_t nextShellIdentity() noexcept
        {
            std::uint64_t value = nextShellIdentityValue.fetch_add(1, std::memory_order_relaxed);
            if(value == 0)
            {
                value = nextShellIdentityValue.fetch_add(1, std::memory_order_relaxed);
            }
            return value;
        }

        /// @brief Recursively validates and owns a tray menu while deduplicating command IDs.
        [[nodiscard]] GameWIP::Status::Types::Status copyMenuItems(
            std::span<const Types::Tray::MenuItem> source,
            std::vector<OwnedTrayMenuItem> &out,
            std::unordered_set<std::uint32_t> &ids) noexcept
        {
            try
            {
                out.clear();
                out.reserve(source.size());

                for(const Types::Tray::MenuItem &item : source)
                {
                    const bool actionable = item.kind == Types::Tray::MenuItemKind::Command || item.kind == Types::Tray::MenuItemKind::Check ||
                                            item.kind == Types::Tray::MenuItemKind::Radio;

                    // Validate each item before retaining any of its data.
                    if(actionable && (!item.commandId.isValid() || !validText(item.label, true) || !item.children.empty()))
                    {
                        return error(ErrorCode::InvalidArgument);
                    }

                    if(item.kind == Types::Tray::MenuItemKind::Submenu &&
                       (item.commandId.isValid() || !validText(item.label, true) || item.children.empty() || !item.accelerator.empty()))
                    {
                        return error(ErrorCode::InvalidArgument);
                    }

                    if(item.kind == Types::Tray::MenuItemKind::Separator &&
                       (item.commandId.isValid() || !item.label.empty() || item.icon.has_value() || !item.accelerator.empty() ||
                        !item.badge.empty() || !item.children.empty() || !item.enabled))
                    {
                        return error(ErrorCode::InvalidArgument);
                    }

                    if(!validText(item.label) || !validText(item.accelerator) || !validText(item.badge))
                    {
                        return error(ErrorCode::InvalidArgument);
                    }

                    if(actionable && !ids.insert(item.commandId.value).second)
                    {
                        return error(ErrorCode::InvalidArgument);
                    }
                    OwnedTrayMenuItem owned;
                    owned.kind = item.kind;
                    owned.commandId = item.commandId;
                    owned.label = item.label;
                    owned.accelerator = item.accelerator;
                    owned.badge = item.badge;
                    owned.enabled = item.enabled;
                    owned.checked = item.checked;

                    // Copy an optional icon before recursively owning child items.
                    if(item.icon.has_value())
                    {
                        owned.icon.emplace();
                        GameWIP::Status::Types::Status status = copyIcon(*item.icon, *owned.icon);

                        if(!status.ok())
                        {
                            return status;
                        }
                    }

                    GameWIP::Status::Types::Status status = copyMenuItems(item.children, owned.children, ids);

                    if(!status.ok())
                    {
                        return status;
                    }
                    out.push_back(std::move(owned));
                }

                return GameWIP::Status::Types::Status{};
            }
            catch(const std::bad_alloc &)
            {
                return error(ErrorCode::OutOfMemory);
            }
            catch(...)
            {
                return error(ErrorCode::Unknown);
            }
        }

        /// @brief Validates and copies tray icons, tooltip, and recursive menu data.
        [[nodiscard]] GameWIP::Status::Types::Status copyDescription(const Types::Tray::Description &description, TrayIconState &state) noexcept
        {
            if(description.icons.empty() || !validText(description.tooltip))
            {
                return error(ErrorCode::InvalidArgument);
            }

            try
            {
                state.icons.clear();
                state.icons.reserve(description.icons.size());

                for(Types::IconImageView icon : description.icons)
                {
                    OwnedIconImage owned;
                    GameWIP::Status::Types::Status status = copyIcon(icon, owned);

                    if(!status.ok())
                    {
                        return status;
                    }
                    state.icons.push_back(std::move(owned));
                }

                state.tooltip = description.tooltip;
                std::unordered_set<std::uint32_t> ids;
                return copyMenuItems(description.menu.items, state.menu, ids);
            }
            catch(const std::bad_alloc &)
            {
                return error(ErrorCode::OutOfMemory);
            }
            catch(...)
            {
                return error(ErrorCode::Unknown);
            }
        }
    } // namespace
} // namespace GameWIP::Desktop::Internal

namespace GameWIP::Desktop
{
    /// ------------------------------------------------------------
    /// Tray ownership checks
    /// ------------------------------------------------------------

    namespace
    {
        using Internal::TrayIconState;
        using ErrorCode = GameWIP::Status::Types::Code;

        /// @brief Builds a portable status from a single error code.
        [[nodiscard]] GameWIP::Status::Types::Status error(ErrorCode code) noexcept
        {
            return GameWIP::Desktop::Internal::statusFor(code);
        }

        /// @brief Validates an open tray icon and its owner-thread access.
        [[nodiscard]] GameWIP::Status::Types::Status requireOwnedTray(TrayIconState *state, bool owned) noexcept
        {
            return Internal::requireOwnedShellResource(state != nullptr && state->open && state->platform != nullptr, owned);
        }
    } // namespace

    /// ------------------------------------------------------------
    /// Tray integration
    /// ------------------------------------------------------------

    TrayIcon::TrayIcon() noexcept = default;

    TrayIcon::~TrayIcon() noexcept
    {
        if(state_ == nullptr)
        {
            return;
        }

        if(ownedByCurrentThread())
        {
            static_cast<void>(close());
        }
        else
        {
            Platform::closeTrayBestEffort(*state_);
            Internal::unbindQueue(state_->eventQueue);
            state_.reset();
        }
    }

    GameWIP::Status::Types::Result<void> TrayIcon::open(const Types::Tray::Description &description) noexcept
    {
        // A menu needs an event queue to deliver command responses.
        if(!description.menu.items.empty())
        {
            return error(ErrorCode::InvalidArgument);
        }

        if(state_ != nullptr)
        {
            return error(ErrorCode::AlreadyOpen);
        }

        try
        {
            // Copy the description and create the native tray icon before publishing state.
            auto candidate = std::make_unique<Internal::TrayIconState>();
            candidate->ownerThread = std::this_thread::get_id();
            candidate->id = Types::Shell::TrayIconId{Internal::nextShellIdentity()};
            GameWIP::Status::Types::Status status = Internal::copyDescription(description, *candidate);

            if(!status.ok())
            {
                return status;
            }
            candidate->open = true;
            status = Platform::openTray(*candidate);

            if(!status.ok())
            {
                Platform::closeTrayBestEffort(*candidate);
                return status;
            }
            state_ = std::move(candidate);
            return GameWIP::Status::Types::Status{};
        }
        catch(const std::bad_alloc &)
        {
            return error(ErrorCode::OutOfMemory);
        }
        catch(...)
        {
            return error(ErrorCode::Unknown);
        }
    }

    GameWIP::Status::Types::Result<void> TrayIcon::open(const Types::Tray::Description &description, ShellEventQueue &eventQueue) noexcept
    {
        if(state_ != nullptr)
        {
            return error(ErrorCode::AlreadyOpen);
        }

        try
        {
            // Copy the description and bind its response queue.
            auto candidate = std::make_unique<Internal::TrayIconState>();
            candidate->ownerThread = std::this_thread::get_id();
            candidate->id = Types::Shell::TrayIconId{Internal::nextShellIdentity()};
            GameWIP::Status::Types::Status status = Internal::copyDescription(description, *candidate);

            if(!status.ok())
            {
                return status;
            }

            status = Internal::bindQueue(eventQueue, candidate->ownerThread, candidate->eventQueue);

            if(!status.ok())
            {
                return status;
            }
            candidate->open = true;
            status = Platform::openTray(*candidate);

            if(!status.ok())
            {
                Internal::unbindQueue(candidate->eventQueue);
                Platform::closeTrayBestEffort(*candidate);
                return status;
            }
            candidate->menuWasPublished = !candidate->menu.empty();

            // Publish the tray state after native initialization succeeds.
            state_ = std::move(candidate);
            return GameWIP::Status::Types::Status{};
        }
        catch(const std::bad_alloc &)
        {
            return error(ErrorCode::OutOfMemory);
        }
        catch(...)
        {
            return error(ErrorCode::Unknown);
        }
    }

    bool TrayIcon::isOpen() const noexcept
    {
        return state_ != nullptr && state_->open && state_->platform != nullptr;
    }

    Types::Shell::TrayIconId TrayIcon::id() const noexcept
    {
        return state_ != nullptr ? state_->id : Types::Shell::TrayIconId{};
    }

    bool TrayIcon::ownedByCurrentThread() const noexcept
    {
        return state_ != nullptr && state_->ownerThread == std::this_thread::get_id();
    }

    bool TrayIcon::hasEventQueue() const noexcept
    {
        return state_ != nullptr && state_->eventQueue != nullptr;
    }

    GameWIP::Status::Types::Result<void> TrayIcon::bindEventQueue(ShellEventQueue &eventQueue) noexcept
    {
        GameWIP::Status::Types::Status status = requireOwnedTray(Internal::TrayIconAccess::state(*this), ownedByCurrentThread());

        if(!status.ok())
        {
            return status;
        }

        if(state_->eventQueue != nullptr || state_->menuWasPublished)
        {
            return error(ErrorCode::InvalidArgument);
        }
        return Internal::bindQueue(eventQueue, state_->ownerThread, state_->eventQueue);
    }

    GameWIP::Status::Types::Result<void> TrayIcon::close() noexcept
    {
        if(state_ == nullptr)
        {
            return GameWIP::Status::Types::Status{};
        }

        if(!ownedByCurrentThread())
        {
            return error(ErrorCode::ResourceBusy);
        }

        const GameWIP::Status::Types::Status status = Platform::closeTray(*state_);

        if(!status.ok())
        {
            return status;
        }

        Internal::unbindQueue(state_->eventQueue);
        state_.reset();
        return GameWIP::Status::Types::Status{};
    }

    GameWIP::Status::Types::Result<void> TrayIcon::setIcon(std::span<const Types::IconImageView> icons) noexcept
    {
        GameWIP::Status::Types::Status status = requireOwnedTray(Internal::TrayIconAccess::state(*this), ownedByCurrentThread());

        if(!status.ok())
        {
            return status;
        }

        if(icons.empty())
        {
            return error(ErrorCode::InvalidArgument);
        }

        try
        {
            // Copy the replacement icon set before changing retained state.
            std::vector<Internal::OwnedIconImage> replacement;
            replacement.reserve(icons.size());

            for(Types::IconImageView icon : icons)
            {
                Internal::OwnedIconImage owned;
                status = Internal::copyIcon(icon, owned);

                if(!status.ok())
                {
                    return status;
                }
                replacement.push_back(std::move(owned));
            }

            auto old = std::move(state_->icons);
            state_->icons = std::move(replacement);

            // Apply the native update and restore the prior icons on failure.
            status = Platform::applyTray(*state_);

            if(!status.ok())
            {
                state_->icons = std::move(old);
            }

            return status;
        }
        catch(const std::bad_alloc &)
        {
            return error(ErrorCode::OutOfMemory);
        }
        catch(...)
        {
            return error(ErrorCode::Unknown);
        }
    }

    GameWIP::Status::Types::Result<void> TrayIcon::setTooltip(std::string_view tooltip) noexcept
    {
        GameWIP::Status::Types::Status status = requireOwnedTray(Internal::TrayIconAccess::state(*this), ownedByCurrentThread());

        if(!status.ok())
        {
            return status;
        }

        if(!Internal::validText(tooltip))
        {
            return error(ErrorCode::InvalidArgument);
        }

        try
        {
            // Replace the tooltip only after it passes portable text validation.
            std::string old = std::move(state_->tooltip);
            state_->tooltip = tooltip;
            status = Platform::applyTray(*state_);

            if(!status.ok())
            {
                // Restore the prior value if the native update fails.
                state_->tooltip = std::move(old);
            }

            return status;
        }
        catch(const std::bad_alloc &)
        {
            return error(ErrorCode::OutOfMemory);
        }
        catch(...)
        {
            return error(ErrorCode::Unknown);
        }
    }

    GameWIP::Status::Types::Result<void> TrayIcon::setMenu(const Types::Tray::MenuDescription &menu) noexcept
    {
        GameWIP::Status::Types::Status status = requireOwnedTray(Internal::TrayIconAccess::state(*this), ownedByCurrentThread());

        if(!status.ok())
        {
            return status;
        }

        if(!menu.items.empty() && state_->eventQueue == nullptr)
        {
            return error(ErrorCode::InvalidArgument);
        }

        try
        {
            // Copy the menu before applying it to the native tray icon.
            std::vector<Internal::OwnedTrayMenuItem> replacement;
            std::unordered_set<std::uint32_t> ids;
            status = Internal::copyMenuItems(menu.items, replacement, ids);

            if(!status.ok())
            {
                return status;
            }
            auto old = std::move(state_->menu);
            state_->menu = std::move(replacement);

            // Restore the prior menu if native publication fails.
            status = Platform::applyTray(*state_);

            if(!status.ok())
            {
                state_->menu = std::move(old);
            }
            else if(!state_->menu.empty())
            {
                state_->menuWasPublished = true;
            }

            return status;
        }
        catch(const std::bad_alloc &)
        {
            return error(ErrorCode::OutOfMemory);
        }
        catch(...)
        {
            return error(ErrorCode::Unknown);
        }
    }
} // namespace GameWIP::Desktop
