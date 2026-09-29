/// @file shell.cpp
/// @brief Portable shell queue, capability, and resource lifecycle implementation.

#include "desktop/shell.h"

#include "desktop/internal/shell_platform.h"
#include "desktop/internal/shell_state.h"
#include "desktop/internal/window_platform.h"
#include "desktop/internal/window_state.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <new>
#include <unordered_set>
#include <utility>

namespace GameWIP::Desktop::Detail
{
    // ------------------------------------------------------------
    // Queue, resource-copy, and launch validation helpers
    // ------------------------------------------------------------

    namespace
    {
        std::atomic_uint64_t nextShellIdentityValue{1};

        /// @brief Allocates a nonzero process-local shell identity, skipping wraparound zero.
        [[nodiscard]] std::uint64_t nextShellIdentity() noexcept
        {
            std::uint64_t value = nextShellIdentityValue.fetch_add(1, std::memory_order_relaxed);
            if (value == 0)
            {
                value = nextShellIdentityValue.fetch_add(1, std::memory_order_relaxed);
            }
            return value;
        }

        using IO::Types::ErrorCode;

        /// @brief Builds a portable status from a single error code.
        [[nodiscard]] IO::Types::Status error(ErrorCode code) noexcept
        {
            return IO::makeStatus(code);
        }

        /// @brief Returns whether the current thread owns an open shell event queue.
        [[nodiscard]] bool ownedByCurrentThread(const ShellEventQueueState &state) noexcept
        {
            return state.open && state.ownerThread == std::this_thread::get_id();
        }

        /// @brief Maps a logical queue position to its fixed-storage ring slot.
        [[nodiscard]] std::size_t physicalIndex(const ShellEventQueueState &state, std::size_t logicalIndex) noexcept
        {
            return (state.eventHead + logicalIndex) % state.eventStorage.size();
        }

        /// @brief Clears queue payloads while the caller holds the queue mutex.
        void clearEventsUnlocked(ShellEventQueueState &state) noexcept
        {
            for (std::size_t index = 0; index < state.eventCount; ++index)
            {
                state.eventStorage[physicalIndex(state, index)] = {};
            }
            state.eventHead = 0;
            state.eventCount = 0;
        }

        /// @brief Validates shell text and optionally requires a non-empty value.
        [[nodiscard]] bool validText(std::string_view text, bool required = false) noexcept
        {
            return (!required || !text.empty()) && !text.contains('\0');
        }

        /// @brief Validates optional progress state before native shell publication.
        [[nodiscard]] IO::Types::Status validateProgress(const std::optional<Types::Shell::Progress> &progress) noexcept
        {
            if (!progress.has_value())
            {
                return IO::successStatus();
            }
            if (!std::isfinite(progress->fraction))
            {
                return error(ErrorCode::InvalidArgument);
            }
            if (progress->state != Types::Shell::ProgressState::Indeterminate && (progress->fraction < 0.0 || progress->fraction > 1.0))
            {
                return error(ErrorCode::InvalidArgument);
            }
            return IO::successStatus();
        }

        /// @brief Computes checked RGBA8 storage size for a shell image.
        [[nodiscard]] bool imageByteCount(Types::PixelSize size, std::size_t &count) noexcept
        {
            constexpr std::size_t bytesPerPixel = 4;
            if (size.width == 0 || size.height == 0 ||
                static_cast<std::size_t>(size.width) > std::numeric_limits<std::size_t>::max() / bytesPerPixel / size.height)
            {
                return false;
            }
            count = static_cast<std::size_t>(size.width) * static_cast<std::size_t>(size.height) * bytesPerPixel;
            return true;
        }

        /// @brief Validates and deep-copies a call-scoped icon view into retained state.
        [[nodiscard]] IO::Types::Status copyIcon(Types::IconImageView view, OwnedIconImage &out) noexcept
        {
            std::size_t byteCount = 0;
            if (!imageByteCount(view.size, byteCount) || view.rgba8.size() != byteCount)
            {
                return error(ErrorCode::InvalidArgument);
            }
            try
            {
                out.size = view.size;
                out.rgba8.assign(view.rgba8.begin(), view.rgba8.end());
                return IO::successStatus();
            }
            catch (const std::bad_alloc &)
            {
                return error(ErrorCode::OutOfMemory);
            }
            catch (...)
            {
                return error(ErrorCode::Unknown);
            }
        }

        /// @brief Binds one shell resource to an owner-thread queue and increments its bind count.
        [[nodiscard]] IO::Types::Status bindQueue(ShellEventQueue &queue, std::thread::id owner, ShellEventQueueState *&outQueue) noexcept
        {
            ShellEventQueueState *candidate = ShellEventQueueAccess::state(queue);
            if (candidate == nullptr)
            {
                return error(ErrorCode::NotOpen);
            }
            std::lock_guard lock(candidate->mutex);
            if (!candidate->open)
            {
                return error(ErrorCode::NotOpen);
            }
            if (candidate->ownerThread != owner || owner != std::this_thread::get_id())
            {
                return error(ErrorCode::ResourceBusy);
            }
            ++candidate->boundResourceCount;
            outQueue = candidate;
            return IO::successStatus();
        }

        /// @brief Drops one resource binding without closing or destroying the queue.
        void unbindQueue(ShellEventQueueState *queue) noexcept
        {
            if (queue == nullptr)
            {
                return;
            }
            std::lock_guard lock(queue->mutex);
            if (queue->boundResourceCount != 0)
            {
                --queue->boundResourceCount;
            }
        }

        /// @brief Validates and owns taskbar thumbnail buttons with unique native IDs.
        [[nodiscard]] IO::Types::Status validateThumbnailButtons(
            std::span<const Types::Taskbar::ThumbnailButton> buttons,
            std::vector<OwnedThumbnailButton> &out) noexcept
        {
            try
            {
                std::unordered_set<std::uint32_t> ids;
                out.clear();
                out.reserve(buttons.size());
                for (const Types::Taskbar::ThumbnailButton &button : buttons)
                {
                    if (!button.id.isValid() || button.id.value > std::numeric_limits<std::uint16_t>::max() || !validText(button.label, true) ||
                        !ids.insert(button.id.value).second)
                    {
                        return error(ErrorCode::InvalidArgument);
                    }
                    OwnedThumbnailButton owned;
                    owned.id = button.id;
                    owned.label = button.label;
                    owned.enabled = button.enabled;
                    if (button.icon.has_value())
                    {
                        owned.icon.emplace();
                        IO::Types::Status status = copyIcon(*button.icon, *owned.icon);
                        if (!status.ok())
                        {
                            return status;
                        }
                    }
                    out.push_back(std::move(owned));
                }
                return IO::successStatus();
            }
            catch (const std::bad_alloc &)
            {
                return error(ErrorCode::OutOfMemory);
            }
            catch (...)
            {
                return error(ErrorCode::Unknown);
            }
        }

        /// @brief Recursively validates and owns a tray menu while deduplicating command IDs.
        [[nodiscard]] IO::Types::Status copyMenuItems(
            std::span<const Types::Tray::MenuItem> source,
            std::vector<OwnedTrayMenuItem> &out,
            std::unordered_set<std::uint32_t> &ids) noexcept
        {
            try
            {
                out.clear();
                out.reserve(source.size());
                for (const Types::Tray::MenuItem &item : source)
                {
                    const bool actionable = item.kind == Types::Tray::MenuItemKind::Command || item.kind == Types::Tray::MenuItemKind::Check ||
                                            item.kind == Types::Tray::MenuItemKind::Radio;
                    if (actionable && (!item.commandId.isValid() || !validText(item.label, true) || !item.children.empty()))
                    {
                        return error(ErrorCode::InvalidArgument);
                    }
                    if (item.kind == Types::Tray::MenuItemKind::Submenu &&
                        (item.commandId.isValid() || !validText(item.label, true) || item.children.empty() || !item.accelerator.empty()))
                    {
                        return error(ErrorCode::InvalidArgument);
                    }
                    if (item.kind == Types::Tray::MenuItemKind::Separator &&
                        (item.commandId.isValid() || !item.label.empty() || item.icon.has_value() || !item.accelerator.empty() ||
                         !item.badge.empty() || !item.children.empty() || !item.enabled))
                    {
                        return error(ErrorCode::InvalidArgument);
                    }
                    if (!validText(item.label) || !validText(item.accelerator) || !validText(item.badge))
                    {
                        return error(ErrorCode::InvalidArgument);
                    }
                    if (actionable && !ids.insert(item.commandId.value).second)
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
                    if (item.icon.has_value())
                    {
                        owned.icon.emplace();
                        IO::Types::Status status = copyIcon(*item.icon, *owned.icon);
                        if (!status.ok())
                        {
                            return status;
                        }
                    }
                    IO::Types::Status status = copyMenuItems(item.children, owned.children, ids);
                    if (!status.ok())
                    {
                        return status;
                    }
                    out.push_back(std::move(owned));
                }
                return IO::successStatus();
            }
            catch (const std::bad_alloc &)
            {
                return error(ErrorCode::OutOfMemory);
            }
            catch (...)
            {
                return error(ErrorCode::Unknown);
            }
        }

        /// @brief Validates and copies taskbar description data into stable state.
        [[nodiscard]] IO::Types::Status copyDescription(const Types::Taskbar::Description &description, TaskbarItemState &state) noexcept
        {
            IO::Types::Status status = validateProgress(description.progress);
            if (!status.ok())
            {
                return status;
            }
            state.progress = description.progress;
            state.overlayIcon.reset();
            if (description.overlayIcon.has_value())
            {
                state.overlayIcon.emplace();
                status = copyIcon(*description.overlayIcon, *state.overlayIcon);
                if (!status.ok())
                {
                    return status;
                }
            }
            return validateThumbnailButtons(description.thumbnailButtons, state.thumbnailButtons);
        }

        /// @brief Validates and copies tray icons, tooltip, and recursive menu data.
        [[nodiscard]] IO::Types::Status copyDescription(const Types::Tray::Description &description, TrayIconState &state) noexcept
        {
            if (description.icons.empty() || !validText(description.tooltip))
            {
                return error(ErrorCode::InvalidArgument);
            }
            try
            {
                state.icons.clear();
                state.icons.reserve(description.icons.size());
                for (Types::IconImageView icon : description.icons)
                {
                    OwnedIconImage owned;
                    IO::Types::Status status = copyIcon(icon, owned);
                    if (!status.ok())
                    {
                        return status;
                    }
                    state.icons.push_back(std::move(owned));
                }
                state.tooltip = description.tooltip;
                std::unordered_set<std::uint32_t> ids;
                return copyMenuItems(description.menu.items, state.menu, ids);
            }
            catch (const std::bad_alloc &)
            {
                return error(ErrorCode::OutOfMemory);
            }
            catch (...)
            {
                return error(ErrorCode::Unknown);
            }
        }

        /// @brief Validates and deep-copies notification text, actions, inputs, media, and scheduling.
        [[nodiscard]] IO::Types::Status copyNotificationDescription(
            const Types::Notifications::Description &description,
            OwnedNotificationDescription &out) noexcept
        {
            if (!validText(description.title, true) || !validText(description.body) || !validText(description.group) || !validText(description.badge))
            {
                return error(ErrorCode::InvalidArgument);
            }
            IO::Types::Status status = validateProgress(description.progress);
            if (!status.ok())
            {
                return status;
            }
            if (description.schedule.has_value() &&
                (description.schedule->occurrenceCount == 0 ||
                 (description.schedule->occurrenceCount > 1 && description.schedule->repeatInterval <= std::chrono::milliseconds::zero())))
            {
                return error(ErrorCode::InvalidArgument);
            }

            try
            {
                std::unordered_set<std::uint32_t> actionIds;
                std::unordered_set<std::uint32_t> inputIds;
                out.title = description.title;
                out.body = description.body;
                out.group = description.group;
                out.badge = description.badge;
                out.sound = description.sound;
                out.urgency = description.urgency;
                out.schedule = description.schedule;
                out.progress = description.progress;
                out.actions.clear();
                out.inputs.clear();
                out.media.clear();
                out.actions.reserve(description.actions.size());
                out.inputs.reserve(description.inputs.size());
                out.media.reserve(description.media.size());
                for (const Types::Notifications::Action &action : description.actions)
                {
                    if (!action.id.isValid() || !validText(action.label, true) || !actionIds.insert(action.id.value).second)
                    {
                        return error(ErrorCode::InvalidArgument);
                    }
                    out.actions.push_back({action.id, std::string(action.label)});
                }
                for (const Types::Notifications::Input &input : description.inputs)
                {
                    if (!input.id.isValid() || !validText(input.placeholder) || !validText(input.initialValue) ||
                        !inputIds.insert(input.id.value).second)
                    {
                        return error(ErrorCode::InvalidArgument);
                    }
                    out.inputs.push_back({input.id, std::string(input.placeholder), std::string(input.initialValue), input.secure});
                }
                for (const Types::Notifications::MediaView &media : description.media)
                {
                    OwnedNotificationMedia owned;
                    if (const auto *image = std::get_if<Types::DataTransfer::ImageView>(&media))
                    {
                        const std::size_t packedRowBytes = static_cast<std::size_t>(image->size.width) * 4U;
                        const std::size_t rowStride = image->rowStrideBytes == 0 ? packedRowBytes : image->rowStrideBytes;
                        if (image->size.width == 0 || image->size.height == 0 || rowStride < packedRowBytes ||
                            static_cast<std::size_t>(image->size.height) > std::numeric_limits<std::size_t>::max() / rowStride ||
                            image->rgba8.size() != static_cast<std::size_t>(image->size.height) * rowStride)
                        {
                            return error(ErrorCode::InvalidArgument);
                        }
                        owned.size = image->size;
                        owned.rgba8.resize(static_cast<std::size_t>(image->size.width) * static_cast<std::size_t>(image->size.height) * 4U);
                        for (std::uint32_t row = 0; row < image->size.height; ++row)
                        {
                            std::copy_n(
                                image->rgba8.data() + static_cast<std::size_t>(row) * rowStride,
                                packedRowBytes,
                                owned.rgba8.data() + static_cast<std::size_t>(row) * packedRowBytes);
                        }
                    }
                    else
                    {
                        const auto &uri = std::get<Types::Notifications::UriMediaView>(media);
                        if (!validText(uri.uri, true))
                        {
                            return error(ErrorCode::InvalidArgument);
                        }
                        owned.uri = true;
                        owned.uriText = uri.uri;
                    }
                    out.media.push_back(std::move(owned));
                }
                return IO::successStatus();
            }
            catch (const std::bad_alloc &)
            {
                return error(ErrorCode::OutOfMemory);
            }
            catch (...)
            {
                return error(ErrorCode::Unknown);
            }
        }

        /// @brief Returns whether a launch action contains a target-substitution placeholder.
        [[nodiscard]] bool containsPlaceholder(const Types::Shell::LaunchActionView &action) noexcept
        {
            for (const Types::Shell::LaunchArgumentView &argument : action.arguments)
            {
                if (std::holds_alternative<Types::Shell::LaunchPlaceholder>(argument))
                {
                    return true;
                }
            }
            return false;
        }

        /// @brief Validates launch targets and arguments before shell registration or publication.
        [[nodiscard]] IO::Types::Status validateLaunchAction(const Types::Shell::LaunchActionView &action, bool allowPlaceholders) noexcept
        {
            if (action.executable.path.get().empty() || (!allowPlaceholders && containsPlaceholder(action)))
            {
                return error(ErrorCode::InvalidArgument);
            }
            for (const Types::Shell::LaunchArgumentView &argument : action.arguments)
            {
                if (const auto *literal = std::get_if<Types::Shell::LiteralLaunchArgumentView>(&argument))
                {
                    if (!validText(literal->text))
                    {
                        return error(ErrorCode::InvalidArgument);
                    }
                }
                else if (const auto *target = std::get_if<Types::Shell::TargetLaunchArgumentView>(&argument))
                {
                    if (const auto *path = std::get_if<Types::Shell::PathTargetView>(&target->target); path != nullptr && path->path.get().empty())
                    {
                        return error(ErrorCode::InvalidArgument);
                    }
                    if (const auto *uri = std::get_if<Types::Shell::UriTargetView>(&target->target); uri != nullptr && !validText(uri->uri, true))
                    {
                        return error(ErrorCode::InvalidArgument);
                    }
                    if (std::holds_alternative<std::monostate>(target->target))
                    {
                        return error(ErrorCode::InvalidArgument);
                    }
                }
            }
            return IO::successStatus();
        }

    } // namespace
} // namespace GameWIP::Desktop::Detail

namespace GameWIP::Desktop
{
    // ------------------------------------------------------------
    // Resource ownership and validation helpers
    // ------------------------------------------------------------

    namespace
    {
        using Detail::NotificationCenterState;
        using Detail::TaskbarItemState;
        using Detail::TrayIconState;
        using IO::Types::ErrorCode;

        /// @brief Builds a portable status from a single error code.
        [[nodiscard]] IO::Types::Status error(ErrorCode code) noexcept
        {
            return IO::makeStatus(code);
        }

        /// @brief Releases the event-queue binding retained by one taskbar item.
        void releaseTaskbarQueue(TaskbarItemState &state) noexcept
        {
            Detail::unbindQueue(state.eventQueue);
            state.eventQueue = nullptr;
        }

        /// @brief Releases the event-queue binding retained by one tray icon.
        void releaseTrayQueue(TrayIconState &state) noexcept
        {
            Detail::unbindQueue(state.eventQueue);
            state.eventQueue = nullptr;
        }

        /// @brief Releases the event-queue binding retained by one notification center.
        void releaseNotificationQueue(NotificationCenterState &state) noexcept
        {
            Detail::unbindQueue(state.eventQueue);
            state.eventQueue = nullptr;
        }

        /// @brief Validates an open taskbar item and its owner-thread access.
        [[nodiscard]] IO::Types::Status requireOwnedTaskbar(TaskbarItemState *state, bool owned) noexcept
        {
            if (state == nullptr || !state->open || state->platform == nullptr || state->window == nullptr)
            {
                return error(ErrorCode::NotOpen);
            }
            return owned ? IO::successStatus() : error(ErrorCode::ResourceBusy);
        }

        /// @brief Validates an open tray icon and its owner-thread access.
        [[nodiscard]] IO::Types::Status requireOwnedTray(TrayIconState *state, bool owned) noexcept
        {
            if (state == nullptr || !state->open || state->platform == nullptr)
            {
                return error(ErrorCode::NotOpen);
            }
            return owned ? IO::successStatus() : error(ErrorCode::ResourceBusy);
        }

        /// @brief Validates an open notification center and its owner-thread access.
        [[nodiscard]] IO::Types::Status requireOwnedNotification(NotificationCenterState *state, bool owned) noexcept
        {
            if (state == nullptr || !state->open || state->platform == nullptr)
            {
                return error(ErrorCode::NotOpen);
            }
            return owned ? IO::successStatus() : error(ErrorCode::ResourceBusy);
        }
    } // namespace

    // ------------------------------------------------------------
    // Taskbar integration
    // ------------------------------------------------------------

    TaskbarItem::TaskbarItem() noexcept = default;

    TaskbarItem::~TaskbarItem() noexcept
    {
        if (state_ == nullptr)
        {
            return;
        }
        if (ownedByCurrentThread())
        {
            static_cast<void>(close());
        }
        else
        {
            Detail::Platform::closeTaskbarBestEffort(*state_);
            Detail::unbindQueue(state_->eventQueue);
            state_.reset();
        }
    }

    IO::Types::Status TaskbarItem::open(Window &window, const Types::Taskbar::Description &description) noexcept
    {
        if (state_ != nullptr)
        {
            return error(ErrorCode::AlreadyOpen);
        }
        Detail::WindowState *windowState = Detail::WindowAccess::state(window);
        if (windowState == nullptr || !Detail::Platform::hasLiveNativeWindow(*windowState))
        {
            return error(ErrorCode::NotOpen);
        }
        if (!Detail::Platform::ownedByCurrentThread(*windowState))
        {
            return error(ErrorCode::ResourceBusy);
        }
        if (!description.thumbnailButtons.empty())
        {
            return error(ErrorCode::InvalidArgument);
        }
        if (windowState->taskbarItem != nullptr)
        {
            return error(ErrorCode::AlreadyExists);
        }
        try
        {
            auto candidate = std::make_unique<Detail::TaskbarItemState>();
            candidate->ownerThread = std::this_thread::get_id();
            candidate->window = windowState;
            candidate->windowId = windowState->id;
            IO::Types::Status status = Detail::copyDescription(description, *candidate);
            if (!status.ok())
            {
                return status;
            }
            candidate->open = true;
            status = Detail::Platform::openTaskbar(*candidate);
            if (!status.ok())
            {
                Detail::Platform::closeTaskbarBestEffort(*candidate);
                return status;
            }
            windowState->taskbarItem = candidate.get();
            state_ = std::move(candidate);
            return IO::successStatus();
        }
        catch (const std::bad_alloc &)
        {
            return error(ErrorCode::OutOfMemory);
        }
        catch (...)
        {
            return error(ErrorCode::Unknown);
        }
    }

    IO::Types::Status TaskbarItem::open(Window &window, const Types::Taskbar::Description &description, ShellEventQueue &eventQueue) noexcept
    {
        if (state_ != nullptr)
        {
            return error(ErrorCode::AlreadyOpen);
        }
        Detail::WindowState *windowState = Detail::WindowAccess::state(window);
        if (windowState == nullptr || !Detail::Platform::hasLiveNativeWindow(*windowState))
        {
            return error(ErrorCode::NotOpen);
        }
        if (!Detail::Platform::ownedByCurrentThread(*windowState))
        {
            return error(ErrorCode::ResourceBusy);
        }
        if (windowState->taskbarItem != nullptr)
        {
            return error(ErrorCode::AlreadyExists);
        }

        try
        {
            auto candidate = std::make_unique<Detail::TaskbarItemState>();
            candidate->ownerThread = std::this_thread::get_id();
            candidate->window = windowState;
            candidate->windowId = windowState->id;
            IO::Types::Status status = Detail::copyDescription(description, *candidate);
            if (!status.ok())
            {
                return status;
            }
            status = Detail::bindQueue(eventQueue, candidate->ownerThread, candidate->eventQueue);
            if (!status.ok())
            {
                return status;
            }
            candidate->open = true;
            status = Detail::Platform::openTaskbar(*candidate);
            if (!status.ok())
            {
                Detail::unbindQueue(candidate->eventQueue);
                Detail::Platform::closeTaskbarBestEffort(*candidate);
                return status;
            }
            windowState->taskbarItem = candidate.get();
            state_ = std::move(candidate);
            return IO::successStatus();
        }
        catch (const std::bad_alloc &)
        {
            return error(ErrorCode::OutOfMemory);
        }
        catch (...)
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

    IO::Types::Status TaskbarItem::close() noexcept
    {
        if (state_ == nullptr)
        {
            return IO::successStatus();
        }
        if (!ownedByCurrentThread())
        {
            return error(ErrorCode::ResourceBusy);
        }
        const IO::Types::Status status = Detail::Platform::closeTaskbar(*state_);
        if (!status.ok())
        {
            return status;
        }
        if (state_->window != nullptr && state_->window->taskbarItem == state_.get())
        {
            state_->window->taskbarItem = nullptr;
        }
        releaseTaskbarQueue(*state_);
        state_.reset();
        return IO::successStatus();
    }

    IO::Types::Status TaskbarItem::setProgress(std::optional<Types::Shell::Progress> progress) noexcept
    {
        IO::Types::Status status = requireOwnedTaskbar(Detail::TaskbarItemAccess::state(*this), ownedByCurrentThread());
        if (!status.ok())
        {
            return status;
        }
        status = Detail::validateProgress(progress);
        if (!status.ok())
        {
            return status;
        }
        const auto old = state_->progress;
        state_->progress = progress;
        status = Detail::Platform::applyTaskbar(*state_);
        if (!status.ok())
        {
            state_->progress = old;
            static_cast<void>(Detail::Platform::applyTaskbar(*state_));
        }
        return status;
    }

    IO::Types::Status TaskbarItem::setOverlayIcon(std::optional<Types::IconImageView> icon) noexcept
    {
        IO::Types::Status status = requireOwnedTaskbar(Detail::TaskbarItemAccess::state(*this), ownedByCurrentThread());
        if (!status.ok())
        {
            return status;
        }
        try
        {
            std::optional<Detail::OwnedIconImage> replacement;
            if (icon.has_value())
            {
                replacement.emplace();
                status = Detail::copyIcon(*icon, *replacement);
                if (!status.ok())
                {
                    return status;
                }
            }
            std::optional<Detail::OwnedIconImage> old;
            old.swap(state_->overlayIcon);
            state_->overlayIcon.swap(replacement);
            status = Detail::Platform::applyTaskbar(*state_);
            if (!status.ok())
            {
                state_->overlayIcon.swap(old);
                static_cast<void>(Detail::Platform::applyTaskbar(*state_));
            }
            return status;
        }
        catch (const std::bad_alloc &)
        {
            return error(ErrorCode::OutOfMemory);
        }
        catch (...)
        {
            return error(ErrorCode::Unknown);
        }
    }

    IO::Types::Status TaskbarItem::setThumbnailButtons(std::span<const Types::Taskbar::ThumbnailButton> buttons) noexcept
    {
        IO::Types::Status status = requireOwnedTaskbar(Detail::TaskbarItemAccess::state(*this), ownedByCurrentThread());
        if (!status.ok())
        {
            return status;
        }
        if (!buttons.empty() && state_->eventQueue == nullptr)
        {
            return error(ErrorCode::InvalidArgument);
        }
        try
        {
            std::vector<Detail::OwnedThumbnailButton> replacement;
            status = Detail::validateThumbnailButtons(buttons, replacement);
            if (!status.ok())
            {
                return status;
            }
            auto old = std::move(state_->thumbnailButtons);
            state_->thumbnailButtons = std::move(replacement);
            status = Detail::Platform::applyTaskbar(*state_);
            if (!status.ok())
            {
                state_->thumbnailButtons = std::move(old);
                static_cast<void>(Detail::Platform::applyTaskbar(*state_));
            }
            return status;
        }
        catch (const std::bad_alloc &)
        {
            return error(ErrorCode::OutOfMemory);
        }
        catch (...)
        {
            return error(ErrorCode::Unknown);
        }
    }

    // ------------------------------------------------------------
    // Tray integration
    // ------------------------------------------------------------

    TrayIcon::TrayIcon() noexcept = default;

    TrayIcon::~TrayIcon() noexcept
    {
        if (state_ == nullptr)
        {
            return;
        }
        if (ownedByCurrentThread())
        {
            static_cast<void>(close());
        }
        else
        {
            Detail::Platform::closeTrayBestEffort(*state_);
            Detail::unbindQueue(state_->eventQueue);
            state_.reset();
        }
    }

    IO::Types::Status TrayIcon::open(const Types::Tray::Description &description) noexcept
    {
        if (!description.menu.items.empty())
        {
            return error(ErrorCode::InvalidArgument);
        }
        if (state_ != nullptr)
        {
            return error(ErrorCode::AlreadyOpen);
        }
        try
        {
            auto candidate = std::make_unique<Detail::TrayIconState>();
            candidate->ownerThread = std::this_thread::get_id();
            candidate->id = Types::Shell::TrayIconId{Detail::nextShellIdentity()};
            IO::Types::Status status = Detail::copyDescription(description, *candidate);
            if (!status.ok())
            {
                return status;
            }
            candidate->open = true;
            status = Detail::Platform::openTray(*candidate);
            if (!status.ok())
            {
                Detail::Platform::closeTrayBestEffort(*candidate);
                return status;
            }
            state_ = std::move(candidate);
            return IO::successStatus();
        }
        catch (const std::bad_alloc &)
        {
            return error(ErrorCode::OutOfMemory);
        }
        catch (...)
        {
            return error(ErrorCode::Unknown);
        }
    }

    IO::Types::Status TrayIcon::open(const Types::Tray::Description &description, ShellEventQueue &eventQueue) noexcept
    {
        if (state_ != nullptr)
        {
            return error(ErrorCode::AlreadyOpen);
        }
        try
        {
            auto candidate = std::make_unique<Detail::TrayIconState>();
            candidate->ownerThread = std::this_thread::get_id();
            candidate->id = Types::Shell::TrayIconId{Detail::nextShellIdentity()};
            IO::Types::Status status = Detail::copyDescription(description, *candidate);
            if (!status.ok())
            {
                return status;
            }
            status = Detail::bindQueue(eventQueue, candidate->ownerThread, candidate->eventQueue);
            if (!status.ok())
            {
                return status;
            }
            candidate->open = true;
            status = Detail::Platform::openTray(*candidate);
            if (!status.ok())
            {
                Detail::unbindQueue(candidate->eventQueue);
                Detail::Platform::closeTrayBestEffort(*candidate);
                return status;
            }
            candidate->menuWasPublished = !candidate->menu.empty();
            state_ = std::move(candidate);
            return IO::successStatus();
        }
        catch (const std::bad_alloc &)
        {
            return error(ErrorCode::OutOfMemory);
        }
        catch (...)
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

    IO::Types::Status TrayIcon::bindEventQueue(ShellEventQueue &eventQueue) noexcept
    {
        IO::Types::Status status = requireOwnedTray(Detail::TrayIconAccess::state(*this), ownedByCurrentThread());
        if (!status.ok())
        {
            return status;
        }
        if (state_->eventQueue != nullptr || state_->menuWasPublished)
        {
            return error(ErrorCode::InvalidArgument);
        }
        return Detail::bindQueue(eventQueue, state_->ownerThread, state_->eventQueue);
    }

    IO::Types::Status TrayIcon::close() noexcept
    {
        if (state_ == nullptr)
        {
            return IO::successStatus();
        }
        if (!ownedByCurrentThread())
        {
            return error(ErrorCode::ResourceBusy);
        }
        const IO::Types::Status status = Detail::Platform::closeTray(*state_);
        if (!status.ok())
        {
            return status;
        }
        releaseTrayQueue(*state_);
        state_.reset();
        return IO::successStatus();
    }

    IO::Types::Status TrayIcon::setIcon(std::span<const Types::IconImageView> icons) noexcept
    {
        IO::Types::Status status = requireOwnedTray(Detail::TrayIconAccess::state(*this), ownedByCurrentThread());
        if (!status.ok())
        {
            return status;
        }
        if (icons.empty())
        {
            return error(ErrorCode::InvalidArgument);
        }
        try
        {
            std::vector<Detail::OwnedIconImage> replacement;
            replacement.reserve(icons.size());
            for (Types::IconImageView icon : icons)
            {
                Detail::OwnedIconImage owned;
                status = Detail::copyIcon(icon, owned);
                if (!status.ok())
                {
                    return status;
                }
                replacement.push_back(std::move(owned));
            }
            auto old = std::move(state_->icons);
            state_->icons = std::move(replacement);
            status = Detail::Platform::applyTray(*state_);
            if (!status.ok())
            {
                state_->icons = std::move(old);
            }
            return status;
        }
        catch (const std::bad_alloc &)
        {
            return error(ErrorCode::OutOfMemory);
        }
        catch (...)
        {
            return error(ErrorCode::Unknown);
        }
    }

    IO::Types::Status TrayIcon::setTooltip(std::string_view tooltip) noexcept
    {
        IO::Types::Status status = requireOwnedTray(Detail::TrayIconAccess::state(*this), ownedByCurrentThread());
        if (!status.ok())
        {
            return status;
        }
        if (!Detail::validText(tooltip))
        {
            return error(ErrorCode::InvalidArgument);
        }
        try
        {
            std::string old = std::move(state_->tooltip);
            state_->tooltip = tooltip;
            status = Detail::Platform::applyTray(*state_);
            if (!status.ok())
            {
                state_->tooltip = std::move(old);
            }
            return status;
        }
        catch (const std::bad_alloc &)
        {
            return error(ErrorCode::OutOfMemory);
        }
        catch (...)
        {
            return error(ErrorCode::Unknown);
        }
    }

    IO::Types::Status TrayIcon::setMenu(const Types::Tray::MenuDescription &menu) noexcept
    {
        IO::Types::Status status = requireOwnedTray(Detail::TrayIconAccess::state(*this), ownedByCurrentThread());
        if (!status.ok())
        {
            return status;
        }
        if (!menu.items.empty() && state_->eventQueue == nullptr)
        {
            return error(ErrorCode::InvalidArgument);
        }
        try
        {
            std::vector<Detail::OwnedTrayMenuItem> replacement;
            std::unordered_set<std::uint32_t> ids;
            status = Detail::copyMenuItems(menu.items, replacement, ids);
            if (!status.ok())
            {
                return status;
            }
            auto old = std::move(state_->menu);
            state_->menu = std::move(replacement);
            status = Detail::Platform::applyTray(*state_);
            if (!status.ok())
            {
                state_->menu = std::move(old);
            }
            else if (!state_->menu.empty())
            {
                state_->menuWasPublished = true;
            }
            return status;
        }
        catch (const std::bad_alloc &)
        {
            return error(ErrorCode::OutOfMemory);
        }
        catch (...)
        {
            return error(ErrorCode::Unknown);
        }
    }

    // ------------------------------------------------------------
    // Notification integration
    // ------------------------------------------------------------

    NotificationCenter::NotificationCenter() noexcept = default;

    NotificationCenter::~NotificationCenter() noexcept
    {
        if (state_ == nullptr)
        {
            return;
        }
        if (ownedByCurrentThread())
        {
            static_cast<void>(close());
        }
        else
        {
            Detail::Platform::closeNotificationCenterBestEffort(*state_);
            Detail::unbindQueue(state_->eventQueue);
            state_.reset();
        }
    }

    IO::Types::Status NotificationCenter::open() noexcept
    {
        if (state_ != nullptr)
        {
            return error(ErrorCode::AlreadyOpen);
        }
        try
        {
            auto candidate = std::make_unique<Detail::NotificationCenterState>();
            candidate->ownerThread = std::this_thread::get_id();
            candidate->open = true;
            const IO::Types::Status status = Detail::Platform::openNotificationCenter(*candidate);
            if (!status.ok())
            {
                Detail::Platform::closeNotificationCenterBestEffort(*candidate);
                return status;
            }
            state_ = std::move(candidate);
            return IO::successStatus();
        }
        catch (const std::bad_alloc &)
        {
            return error(ErrorCode::OutOfMemory);
        }
        catch (...)
        {
            return error(ErrorCode::Unknown);
        }
    }

    IO::Types::Status NotificationCenter::open(ShellEventQueue &eventQueue) noexcept
    {
        if (state_ != nullptr)
        {
            return error(ErrorCode::AlreadyOpen);
        }
        try
        {
            auto candidate = std::make_unique<Detail::NotificationCenterState>();
            candidate->ownerThread = std::this_thread::get_id();
            IO::Types::Status status = Detail::bindQueue(eventQueue, candidate->ownerThread, candidate->eventQueue);
            if (!status.ok())
            {
                return status;
            }
            candidate->open = true;
            status = Detail::Platform::openNotificationCenter(*candidate);
            if (!status.ok())
            {
                Detail::unbindQueue(candidate->eventQueue);
                Detail::Platform::closeNotificationCenterBestEffort(*candidate);
                return status;
            }
            state_ = std::move(candidate);
            return IO::successStatus();
        }
        catch (const std::bad_alloc &)
        {
            return error(ErrorCode::OutOfMemory);
        }
        catch (...)
        {
            return error(ErrorCode::Unknown);
        }
    }

    bool NotificationCenter::isOpen() const noexcept
    {
        return state_ != nullptr && state_->open && state_->platform != nullptr;
    }

    bool NotificationCenter::ownedByCurrentThread() const noexcept
    {
        return state_ != nullptr && state_->ownerThread == std::this_thread::get_id();
    }

    bool NotificationCenter::hasEventQueue() const noexcept
    {
        return state_ != nullptr && state_->eventQueue != nullptr;
    }

    IO::Types::Status NotificationCenter::close() noexcept
    {
        if (state_ == nullptr)
        {
            return IO::successStatus();
        }
        if (!ownedByCurrentThread())
        {
            return error(ErrorCode::ResourceBusy);
        }
        const IO::Types::Status status = Detail::Platform::closeNotificationCenter(*state_);
        if (!status.ok())
        {
            return status;
        }
        releaseNotificationQueue(*state_);
        state_->notifications.clear();
        state_.reset();
        return IO::successStatus();
    }

    Types::Notifications::PublishResult NotificationCenter::publish(const Types::Notifications::Description &description) noexcept
    {
        Types::Notifications::PublishResult result{error(ErrorCode::NotOpen), {}};
        IO::Types::Status status = requireOwnedNotification(Detail::NotificationCenterAccess::state(*this), ownedByCurrentThread());
        if (!status.ok())
        {
            result.status = status;
            return result;
        }
        if ((!description.actions.empty() || !description.inputs.empty()) && state_->eventQueue == nullptr)
        {
            result.status = error(ErrorCode::InvalidArgument);
            return result;
        }
        std::uint64_t publishedId = 0;
        bool nativePublished = false;
        try
        {
            Detail::OwnedNotificationDescription owned;
            status = Detail::copyNotificationDescription(description, owned);
            if (!status.ok())
            {
                result.status = status;
                return result;
            }
            std::uint64_t id = state_->nextId++;
            if (id == 0)
            {
                id = state_->nextId++;
            }
            while (id == 0 || state_->notifications.contains(id))
            {
                id = state_->nextId++;
                if (id == 0)
                {
                    id = state_->nextId++;
                }
            }
            status = Detail::Platform::publishNotification(*state_, id, owned);
            if (!status.ok())
            {
                result.status = status;
                return result;
            }
            publishedId = id;
            nativePublished = true;
            state_->notifications.emplace(id, std::move(owned));
            result.status = IO::successStatus();
            result.id = Types::Shell::NotificationId{id};
            return result;
        }
        catch (const std::bad_alloc &)
        {
            if (nativePublished)
            {
                static_cast<void>(Detail::Platform::dismissNotification(*state_, publishedId));
            }
            result.status = error(ErrorCode::OutOfMemory);
            return result;
        }
        catch (...)
        {
            if (nativePublished)
            {
                static_cast<void>(Detail::Platform::dismissNotification(*state_, publishedId));
            }
            result.status = error(ErrorCode::Unknown);
            return result;
        }
    }

    IO::Types::Status NotificationCenter::update(Types::Shell::NotificationId id, const Types::Notifications::Description &description) noexcept
    {
        IO::Types::Status status = requireOwnedNotification(Detail::NotificationCenterAccess::state(*this), ownedByCurrentThread());
        if (!status.ok())
        {
            return status;
        }
        if (!id.isValid())
        {
            return error(ErrorCode::InvalidArgument);
        }
        if (!state_->notifications.contains(id.value))
        {
            return error(ErrorCode::NotFound);
        }
        if ((!description.actions.empty() || !description.inputs.empty()) && state_->eventQueue == nullptr)
        {
            return error(ErrorCode::InvalidArgument);
        }
        bool nativeUpdated = false;
        std::optional<Detail::OwnedNotificationDescription> previous;
        try
        {
            previous = state_->notifications.at(id.value);
            Detail::OwnedNotificationDescription owned;
            status = Detail::copyNotificationDescription(description, owned);
            if (!status.ok())
            {
                return status;
            }
            status = Detail::Platform::updateNotification(*state_, id.value, owned);
            if (!status.ok())
            {
                return status;
            }
            nativeUpdated = true;
            state_->notifications[id.value] = std::move(owned);
            return IO::successStatus();
        }
        catch (const std::bad_alloc &)
        {
            if (nativeUpdated && previous.has_value())
            {
                static_cast<void>(Detail::Platform::updateNotification(*state_, id.value, *previous));
            }
            return error(ErrorCode::OutOfMemory);
        }
        catch (...)
        {
            if (nativeUpdated && previous.has_value())
            {
                static_cast<void>(Detail::Platform::updateNotification(*state_, id.value, *previous));
            }
            return error(ErrorCode::Unknown);
        }
    }

    IO::Types::Status NotificationCenter::dismiss(Types::Shell::NotificationId id) noexcept
    {
        IO::Types::Status status = requireOwnedNotification(Detail::NotificationCenterAccess::state(*this), ownedByCurrentThread());
        if (!status.ok())
        {
            return status;
        }
        if (!id.isValid())
        {
            return error(ErrorCode::InvalidArgument);
        }
        if (!state_->notifications.contains(id.value))
        {
            return error(ErrorCode::NotFound);
        }
        status = Detail::Platform::dismissNotification(*state_, id.value);
        if (status.ok())
        {
            state_->notifications.erase(id.value);
        }
        return status;
    }

    IO::Types::Status JumpLists::publish(const Types::JumpLists::Description &description) noexcept
    {
        try
        {
            for (const Types::JumpLists::Task &task : description.tasks)
            {
                if (!Detail::validText(task.title, true) || !Detail::validText(task.description))
                {
                    return error(ErrorCode::InvalidArgument);
                }
                IO::Types::Status status = Detail::validateLaunchAction(task.action, false);
                if (!status.ok())
                {
                    return status;
                }
            }
            for (const Types::JumpLists::Category &category : description.categories)
            {
                if (!Detail::validText(category.label, true) || category.tasks.empty())
                {
                    return error(ErrorCode::InvalidArgument);
                }
                for (const Types::JumpLists::Task &task : category.tasks)
                {
                    if (!Detail::validText(task.title, true) || !Detail::validText(task.description))
                    {
                        return error(ErrorCode::InvalidArgument);
                    }
                    IO::Types::Status status = Detail::validateLaunchAction(task.action, false);
                    if (!status.ok())
                    {
                        return status;
                    }
                }
            }
            for (const Types::Shell::TargetView &target : description.recentItems)
            {
                if (const auto *uri = std::get_if<Types::Shell::UriTargetView>(&target); uri != nullptr && !Detail::validText(uri->uri, true))
                {
                    return error(ErrorCode::InvalidArgument);
                }
                if (std::holds_alternative<std::monostate>(target))
                {
                    return error(ErrorCode::InvalidArgument);
                }
            }
            return Detail::Platform::publishJumpLists(description);
        }
        catch (const std::bad_alloc &)
        {
            return error(ErrorCode::OutOfMemory);
        }
        catch (...)
        {
            return error(ErrorCode::Unknown);
        }
    }

    // ------------------------------------------------------------
    // Jump lists and registration
    // ------------------------------------------------------------

    Types::Registration::Result Registration::registerFileExtension(const Types::Registration::FileExtensionDescription &description) noexcept
    {
        Types::Registration::Result result{error(ErrorCode::InvalidArgument), {}, false};
        if (!Detail::validText(description.ownerKey, true) || !Detail::validText(description.extension, true) ||
            !Detail::validText(description.displayName, true) || !Detail::validText(description.contentType))
        {
            return result;
        }
        if (description.defaultAction.has_value())
        {
            IO::Types::Status status = Detail::validateLaunchAction(*description.defaultAction, true);
            if (!status.ok())
            {
                result.status = status;
                return result;
            }
        }
        for (const Types::Registration::Verb &verb : description.verbs)
        {
            if (!Detail::validText(verb.name, true) || !Detail::validText(verb.label, true))
            {
                return result;
            }
            IO::Types::Status status = Detail::validateLaunchAction(verb.action, true);
            if (!status.ok())
            {
                result.status = status;
                return result;
            }
        }
        return Detail::Platform::registerFileExtension(description);
    }

    Types::Registration::Result Registration::registerUriScheme(const Types::Registration::UriSchemeDescription &description) noexcept
    {
        Types::Registration::Result result{error(ErrorCode::InvalidArgument), {}, false};
        if (!Detail::validText(description.ownerKey, true) || !Detail::validText(description.scheme, true) ||
            !Detail::validText(description.displayName, true))
        {
            return result;
        }
        if (description.defaultAction.has_value())
        {
            IO::Types::Status status = Detail::validateLaunchAction(*description.defaultAction, true);
            if (!status.ok())
            {
                result.status = status;
                return result;
            }
        }
        for (const Types::Registration::Verb &verb : description.verbs)
        {
            if (!Detail::validText(verb.name, true) || !Detail::validText(verb.label, true))
            {
                return result;
            }
            IO::Types::Status status = Detail::validateLaunchAction(verb.action, true);
            if (!status.ok())
            {
                result.status = status;
                return result;
            }
        }
        return Detail::Platform::registerUriScheme(description);
    }

    Types::Registration::Result Registration::unregisterFileExtension(std::string_view ownerKey, std::string_view extension) noexcept
    {
        if (!Detail::validText(ownerKey, true) || !Detail::validText(extension, true))
        {
            return {error(ErrorCode::InvalidArgument), {}, false};
        }
        return Detail::Platform::unregisterFileExtension(ownerKey, extension);
    }

    Types::Registration::Result Registration::unregisterUriScheme(std::string_view ownerKey, std::string_view scheme) noexcept
    {
        if (!Detail::validText(ownerKey, true) || !Detail::validText(scheme, true))
        {
            return {error(ErrorCode::InvalidArgument), {}, false};
        }
        return Detail::Platform::unregisterUriScheme(ownerKey, scheme);
    }
} // namespace GameWIP::Desktop

namespace GameWIP::Desktop::Detail
{

    /// @brief Appends one native shell callback payload under the queue lock.
    void publishShellEvent(ShellEventQueueState &queue, Types::Shell::Events::Payload payload) noexcept
    {
        std::lock_guard lock(queue.mutex);
        if (!queue.open || queue.eventStorage.empty())
        {
            return;
        }
        if (queue.eventCount == queue.eventStorage.size())
        {
            ++queue.droppedEvents;
            return;
        }

        try
        {
            const std::size_t slot = physicalIndex(queue, queue.eventCount);
            queue.eventStorage[slot] = Types::Shell::Event{queue.nextSequence++, std::move(payload)};
            ++queue.eventCount;
        }
        catch (...)
        {
            ++queue.droppedEvents;
        }
    }
} // namespace GameWIP::Desktop::Detail

namespace GameWIP::Desktop
{
    // ------------------------------------------------------------
    // Shell event queue
    // ------------------------------------------------------------

    ShellEventQueue::ShellEventQueue() noexcept = default;

    ShellEventQueue::~ShellEventQueue() noexcept
    {
        if (state_ != nullptr)
        {
            static_cast<void>(close());
            state_.reset();
        }
    }

    IO::Types::Status ShellEventQueue::open() noexcept
    {
        return open(Events::kDefaultQueueCapacity);
    }

    IO::Types::Status ShellEventQueue::open(std::size_t capacity) noexcept
    {
        if (capacity == 0)
        {
            return IO::makeStatus(IO::Types::ErrorCode::InvalidArgument);
        }
        if (state_ != nullptr)
        {
            return IO::makeStatus(IO::Types::ErrorCode::AlreadyOpen);
        }

        try
        {
            auto candidate = std::make_unique<Detail::ShellEventQueueState>();
            candidate->internalEvents.resize(capacity);
            candidate->eventStorage = candidate->internalEvents;
            candidate->storageKind = Types::Events::StorageKind::Internal;
            candidate->ownerThread = std::this_thread::get_id();
            candidate->open = true;
            state_ = std::move(candidate);
            return IO::successStatus();
        }
        catch (const std::bad_alloc &)
        {
            return IO::makeStatus(IO::Types::ErrorCode::OutOfMemory);
        }
        catch (...)
        {
            return IO::makeStatus(IO::Types::ErrorCode::Unknown);
        }
    }

    IO::Types::Status ShellEventQueue::open(std::span<Types::Shell::Event> eventStorage) noexcept
    {
        if (eventStorage.empty())
        {
            return IO::makeStatus(IO::Types::ErrorCode::InvalidArgument);
        }
        if (state_ != nullptr)
        {
            return IO::makeStatus(IO::Types::ErrorCode::AlreadyOpen);
        }

        try
        {
            auto candidate = std::make_unique<Detail::ShellEventQueueState>();
            candidate->eventStorage = eventStorage;
            candidate->storageKind = Types::Events::StorageKind::External;
            candidate->ownerThread = std::this_thread::get_id();
            candidate->open = true;
            state_ = std::move(candidate);
            return IO::successStatus();
        }
        catch (const std::bad_alloc &)
        {
            return IO::makeStatus(IO::Types::ErrorCode::OutOfMemory);
        }
        catch (...)
        {
            return IO::makeStatus(IO::Types::ErrorCode::Unknown);
        }
    }

    bool ShellEventQueue::isOpen() const noexcept
    {
        return state_ != nullptr && state_->open;
    }

    bool ShellEventQueue::ownedByCurrentThread() const noexcept
    {
        return state_ != nullptr && Detail::ownedByCurrentThread(*state_);
    }

    IO::Types::Status ShellEventQueue::close() noexcept
    {
        if (state_ == nullptr || !state_->open)
        {
            return IO::successStatus();
        }
        if (!ownedByCurrentThread())
        {
            return IO::makeStatus(IO::Types::ErrorCode::ResourceBusy);
        }

        {
            std::lock_guard lock(state_->mutex);
            if (state_->boundResourceCount != 0)
            {
                return IO::makeStatus(IO::Types::ErrorCode::ResourceBusy);
            }
            Detail::clearEventsUnlocked(*state_);
            state_->eventStorage = {};
            state_->internalEvents.clear();
            state_->open = false;
            state_->ownerThread = {};
        }
        state_.reset();
        return IO::successStatus();
    }

    bool ShellEventQueue::popEvent(Types::Shell::Event &outEvent) noexcept
    {
        if (state_ == nullptr || !ownedByCurrentThread())
        {
            return false;
        }
        std::lock_guard lock(state_->mutex);
        if (!state_->open || state_->eventCount == 0)
        {
            return false;
        }
        try
        {
            outEvent = std::move(state_->eventStorage[state_->eventHead]);
            state_->eventStorage[state_->eventHead] = {};
            state_->eventHead = (state_->eventHead + 1) % state_->eventStorage.size();
            --state_->eventCount;
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    std::size_t ShellEventQueue::popEvents(std::span<Types::Shell::Event> destination) noexcept
    {
        if (state_ == nullptr || destination.empty() || !ownedByCurrentThread())
        {
            return 0;
        }
        std::lock_guard lock(state_->mutex);
        if (!state_->open)
        {
            return 0;
        }

        const std::size_t count = std::min(destination.size(), state_->eventCount);
        std::size_t copied = 0;
        try
        {
            for (; copied < count; ++copied)
            {
                destination[copied] = std::move(state_->eventStorage[state_->eventHead]);
                state_->eventStorage[state_->eventHead] = {};
                state_->eventHead = (state_->eventHead + 1) % state_->eventStorage.size();
                --state_->eventCount;
            }
        }
        catch (...)
        {
            return copied;
        }
        return copied;
    }

    void ShellEventQueue::clearEvents() noexcept
    {
        if (state_ == nullptr || !ownedByCurrentThread())
        {
            return;
        }
        std::lock_guard lock(state_->mutex);
        if (state_->open)
        {
            Detail::clearEventsUnlocked(*state_);
        }
    }

    Types::Events::QueueInfo ShellEventQueue::eventQueueInfo() const noexcept
    {
        if (state_ == nullptr || !state_->open || !ownedByCurrentThread())
        {
            return {};
        }
        std::lock_guard lock(state_->mutex);
        return Types::Events::QueueInfo{state_->storageKind, state_->eventStorage.size(), state_->eventCount, state_->droppedEvents};
    }

    void ShellEventQueue::clearDroppedEventCount() noexcept
    {
        if (state_ == nullptr || !ownedByCurrentThread())
        {
            return;
        }
        std::lock_guard lock(state_->mutex);
        if (state_->open)
        {
            state_->droppedEvents = 0;
        }
    }
} // namespace GameWIP::Desktop

namespace GameWIP::Desktop::Shell
{
    // ------------------------------------------------------------
    // Capability queries
    // ------------------------------------------------------------

    Types::Shell::CapabilitiesResult getCapabilities() noexcept
    {
        return Detail::Platform::getShellCapabilities();
    }

    bool supports(Types::Shell::Capability capability) noexcept
    {
        return getCapabilities().capabilities.supports(capability);
    }
} // namespace GameWIP::Desktop::Shell
