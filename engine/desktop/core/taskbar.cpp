/// @file taskbar.cpp
/// @brief Portable TaskbarItem lifecycle and state forwarding.

#include "desktop/internal/results.h"
#include "desktop/shell_taskbar.h"

#include "desktop/internal/shell_helpers.h"
#include "desktop/internal/shell_state.h"
#include "desktop/internal/window_state.h"
#include "desktop/platform/shell.h"
#include "desktop/platform/window.h"

#include <cstdint>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <span>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

namespace GameWIP::Desktop::Internal
{
    /// ------------------------------------------------------------
    /// Taskbar description helpers
    /// ------------------------------------------------------------

    namespace
    {
        using ErrorCode = GameWIP::Status::Types::Code;

        [[nodiscard]] GameWIP::Status::Types::Status error(ErrorCode code) noexcept
        {
            return GameWIP::Desktop::Internal::statusFor(code);
        }

        /// @brief Validates and owns taskbar thumbnail buttons with unique native IDs.
        [[nodiscard]] GameWIP::Status::Types::Status validateThumbnailButtons(
            std::span<const Types::Taskbar::ThumbnailButton> buttons,
            std::vector<OwnedThumbnailButton> &out) noexcept
        {
            try
            {
                std::unordered_set<std::uint32_t> ids;
                out.clear();
                out.reserve(buttons.size());

                for(const Types::Taskbar::ThumbnailButton &button : buttons)
                {
                    if(!button.id.isValid() || button.id.value > std::numeric_limits<std::uint16_t>::max() || !validText(button.label, true) ||
                       !ids.insert(button.id.value).second)
                    {
                        return error(ErrorCode::InvalidArgument);
                    }
                    OwnedThumbnailButton owned;
                    owned.id = button.id;
                    owned.label = button.label;
                    owned.enabled = button.enabled;

                    if(button.icon.has_value())
                    {
                        owned.icon.emplace();
                        GameWIP::Status::Types::Status status = copyIcon(*button.icon, *owned.icon);

                        if(!status.ok())
                        {
                            return status;
                        }
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

        /// @brief Validates and copies taskbar description data into stable state.
        [[nodiscard]] GameWIP::Status::Types::Status copyDescription(const Types::Taskbar::Description &description, TaskbarItemState &state) noexcept
        {
            GameWIP::Status::Types::Status status = validateProgress(description.progress);

            if(!status.ok())
            {
                return status;
            }

            state.progress = description.progress;
            state.overlayIcon.reset();

            if(description.overlayIcon.has_value())
            {
                OwnedIconImage overlayIcon{};
                status = copyIcon(*description.overlayIcon, overlayIcon);

                if(!status.ok())
                {
                    return status;
                }
                state.overlayIcon.emplace(std::move(overlayIcon));
            }

            return validateThumbnailButtons(description.thumbnailButtons, state.thumbnailButtons);
        }
    } // namespace
} // namespace GameWIP::Desktop::Internal

namespace GameWIP::Desktop
{
    /// ------------------------------------------------------------
    /// Taskbar ownership checks
    /// ------------------------------------------------------------

    namespace
    {
        using Internal::TaskbarItemState;
        using ErrorCode = GameWIP::Status::Types::Code;

        /// @brief Builds a portable status from a single error code.
        [[nodiscard]] GameWIP::Status::Types::Status error(ErrorCode code) noexcept
        {
            return GameWIP::Desktop::Internal::statusFor(code);
        }

        /// @brief Validates an open taskbar item and its owner-thread access.
        [[nodiscard]] GameWIP::Status::Types::Status requireOwnedTaskbar(TaskbarItemState *state, bool owned) noexcept
        {
            return Internal::requireOwnedShellResource(
                state != nullptr && state->open && state->platform != nullptr && state->window != nullptr,
                owned);
        }
    } // namespace

    /// ------------------------------------------------------------
    /// Taskbar integration
    /// ------------------------------------------------------------

    TaskbarItem::TaskbarItem() noexcept = default;

    TaskbarItem::~TaskbarItem() noexcept
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
            Platform::closeTaskbarBestEffort(*state_);
            Internal::unbindQueue(state_->eventQueue);
            state_.reset();
        }
    }

    GameWIP::Status::Types::Result<void> TaskbarItem::open(Window &window, const Types::Taskbar::Description &description) noexcept
    {
        // Validate the Window and reject options that require an event queue.
        if(state_ != nullptr)
        {
            return error(ErrorCode::AlreadyOpen);
        }

        Internal::WindowState *windowState = Internal::WindowAccess::state(window);

        if(windowState == nullptr || !Platform::hasLiveNativeWindow(*windowState))
        {
            return error(ErrorCode::NotOpen);
        }

        if(!Platform::ownedByCurrentThread(*windowState))
        {
            return error(ErrorCode::ResourceBusy);
        }

        if(!description.thumbnailButtons.empty())
        {
            return error(ErrorCode::InvalidArgument);
        }

        if(windowState->taskbarItem != nullptr)
        {
            return error(ErrorCode::AlreadyExists);
        }

        try
        {
            // Prepare owned state and open the native taskbar before publishing the binding.
            auto candidate = std::make_unique<Internal::TaskbarItemState>();
            candidate->ownerThread = std::this_thread::get_id();
            candidate->window = windowState;
            candidate->windowId = windowState->id;
            GameWIP::Status::Types::Status status = Internal::copyDescription(description, *candidate);

            if(!status.ok())
            {
                return status;
            }
            candidate->open = true;
            status = Platform::openTaskbar(*candidate);

            if(!status.ok())
            {
                Platform::closeTaskbarBestEffort(*candidate);
                return status;
            }
            windowState->taskbarItem = candidate.get();
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

    GameWIP::Status::Types::Result<void> TaskbarItem::open(Window &window, const Types::Taskbar::Description &description, ShellEventQueue &eventQueue) noexcept
    {
        // Validate the Window and reject duplicate taskbar bindings.
        if(state_ != nullptr)
        {
            return error(ErrorCode::AlreadyOpen);
        }

        Internal::WindowState *windowState = Internal::WindowAccess::state(window);

        if(windowState == nullptr || !Platform::hasLiveNativeWindow(*windowState))
        {
            return error(ErrorCode::NotOpen);
        }

        if(!Platform::ownedByCurrentThread(*windowState))
        {
            return error(ErrorCode::ResourceBusy);
        }

        if(windowState->taskbarItem != nullptr)
        {
            return error(ErrorCode::AlreadyExists);
        }

        try
        {
            // Prepare retained data and bind the event queue.
            auto candidate = std::make_unique<Internal::TaskbarItemState>();
            candidate->ownerThread = std::this_thread::get_id();
            candidate->window = windowState;
            candidate->windowId = windowState->id;
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
            status = Platform::openTaskbar(*candidate);

            if(!status.ok())
            {
                Internal::unbindQueue(candidate->eventQueue);
                Platform::closeTaskbarBestEffort(*candidate);
                return status;
            }

            // Publish the taskbar binding only after native initialization succeeds.
            windowState->taskbarItem = candidate.get();
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

    bool TaskbarItem::isOpen() const noexcept
    {
        return state_ != nullptr && state_->open && state_->platform != nullptr && state_->window != nullptr;
    }

    Types::WindowId TaskbarItem::windowId() const noexcept
    {
        return state_ != nullptr ? state_->windowId : Types::WindowId{};
    }

    bool TaskbarItem::ownedByCurrentThread() const noexcept
    {
        return state_ != nullptr && state_->ownerThread == std::this_thread::get_id();
    }

    bool TaskbarItem::hasEventQueue() const noexcept
    {
        return state_ != nullptr && state_->eventQueue != nullptr;
    }

    GameWIP::Status::Types::Result<void> TaskbarItem::close() noexcept
    {
        if(state_ == nullptr)
        {
            return GameWIP::Status::Types::Status{};
        }

        if(!ownedByCurrentThread())
        {
            return error(ErrorCode::ResourceBusy);
        }

        const GameWIP::Status::Types::Status status = Platform::closeTaskbar(*state_);

        if(!status.ok())
        {
            return status;
        }

        if(state_->window != nullptr && state_->window->taskbarItem == state_.get())
        {
            state_->window->taskbarItem = nullptr;
        }

        Internal::unbindQueue(state_->eventQueue);
        state_.reset();
        return GameWIP::Status::Types::Status{};
    }

    GameWIP::Status::Types::Result<void> TaskbarItem::setProgress(std::optional<Types::Shell::Progress> progress) noexcept
    {
        GameWIP::Status::Types::Status status = requireOwnedTaskbar(Internal::TaskbarItemAccess::state(*this), ownedByCurrentThread());

        if(!status.ok())
        {
            return status;
        }

        status = Internal::validateProgress(progress);

        if(!status.ok())
        {
            return status;
        }

        const auto old = state_->progress;
        state_->progress = progress;
        status = Platform::applyTaskbar(*state_);

        if(!status.ok())
        {
            state_->progress = old;
            static_cast<void>(Platform::applyTaskbar(*state_));
        }

        return status;
    }

    GameWIP::Status::Types::Result<void> TaskbarItem::setOverlayIcon(std::optional<Types::IconImageView> icon) noexcept
    {
        GameWIP::Status::Types::Status status = requireOwnedTaskbar(Internal::TaskbarItemAccess::state(*this), ownedByCurrentThread());

        if(!status.ok())
        {
            return status;
        }

        try
        {
            // Copy the replacement icon before swapping it into retained state.
            std::optional<Internal::OwnedIconImage> replacement;

            if(icon.has_value())
            {
                replacement.emplace();
                status = Internal::copyIcon(*icon, *replacement);

                if(!status.ok())
                {
                    return status;
                }
            }

            std::optional<Internal::OwnedIconImage> old;
            old.swap(state_->overlayIcon);
            state_->overlayIcon.swap(replacement);
            status = Platform::applyTaskbar(*state_);

            if(!status.ok())
            {
                state_->overlayIcon.swap(old);
                static_cast<void>(Platform::applyTaskbar(*state_));
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

    GameWIP::Status::Types::Result<void> TaskbarItem::setThumbnailButtons(std::span<const Types::Taskbar::ThumbnailButton> buttons) noexcept
    {
        GameWIP::Status::Types::Status status = requireOwnedTaskbar(Internal::TaskbarItemAccess::state(*this), ownedByCurrentThread());

        if(!status.ok())
        {
            return status;
        }

        if(!buttons.empty() && state_->eventQueue == nullptr)
        {
            return error(ErrorCode::InvalidArgument);
        }

        try
        {
            std::vector<Internal::OwnedThumbnailButton> replacement;
            status = Internal::validateThumbnailButtons(buttons, replacement);

            if(!status.ok())
            {
                return status;
            }
            auto old = std::move(state_->thumbnailButtons);
            state_->thumbnailButtons = std::move(replacement);

            // Restore the previous buttons if native publication fails.
            status = Platform::applyTaskbar(*state_);

            if(!status.ok())
            {
                state_->thumbnailButtons = std::move(old);
                static_cast<void>(Platform::applyTaskbar(*state_));
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
