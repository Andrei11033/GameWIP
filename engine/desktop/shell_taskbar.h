/// @file shell_taskbar.h
/// @brief Portable taskbar integration for GameWIP Desktop.

#pragma once

#include "desktop/desktop_export.h"
#include "desktop/shell.h"
#include "desktop/shell_types.h"
#include "desktop/window.h"
#include "io/status.h"

#include <memory>
#include <optional>
#include <span>
#include <string_view>

namespace GameWIP::Desktop
{
    class Window;

    namespace Detail
    {
        struct TaskbarItemAccess;
        struct TaskbarItemState;
    } // namespace Detail
} // namespace GameWIP::Desktop

namespace GameWIP::Desktop::Types::Taskbar
{
    /// @brief One caller-defined taskbar thumbnail command button.
    /// @details The button description is borrowed until the receiving open or replacement
    /// operation returns. Its label and optional icon are copied before publication.
    struct ThumbnailButton
    {
        Shell::CommandId id;               ///< Unique nonzero command identity within one button set.
        std::string_view label;            ///< Nonempty UTF-8 button label.
        std::optional<IconImageView> icon; ///< Optional borrowed RGBA8 icon image.
        bool enabled = true;               ///< Whether the button accepts user activation.
    };

    /// @brief Initial taskbar presentation and thumbnail-button snapshot.
    /// @details All views are copied before open returns. An omitted progress or overlay icon
    /// means that the corresponding display is initially cleared. An empty button span means
    /// that no thumbnail buttons are initially published.
    struct Description
    {
        std::optional<Shell::Progress> progress;           ///< Optional initial progress state.
        std::optional<IconImageView> overlayIcon;          ///< Optional borrowed RGBA8 overlay icon.
        std::span<const ThumbnailButton> thumbnailButtons; ///< Borrowed complete button set; non-empty only with the queue overload.
    };
} // namespace GameWIP::Desktop::Types::Taskbar

namespace GameWIP::Desktop
{
    /// @brief Non-copyable, non-movable owner of one Window's taskbar binding.
    /// @details A successful open binds this item to one open Window lifetime and its owner
    /// thread. At most one TaskbarItem may be open for a Window lifetime. A supplied
    /// ShellEventQueue is borrowed, must be open on the same owner thread, and must outlive
    /// this item until close. Taskbar resources publish no callbacks and do not add shell state
    /// to Window.
    class DESKTOP_EXPORT TaskbarItem final
    {
    public:
        /// @name Lifecycle
        /// @{

        /// @brief Constructs a closed taskbar binding.
        TaskbarItem() noexcept;
        TaskbarItem(const TaskbarItem &) = delete;
        TaskbarItem &operator=(const TaskbarItem &) = delete;
        TaskbarItem(TaskbarItem &&) = delete;
        TaskbarItem &operator=(TaskbarItem &&) = delete;

        /// @brief Best-effort closes the taskbar binding.
        /// @details The caller must release the binding on its owner thread before destruction.
        ~TaskbarItem() noexcept;

        /// @brief Opens without thumbnail-command event delivery.
        /// @details The description must contain no thumbnail buttons. Use the queue overload
        /// to publish thumbnail-command buttons; a non-empty button span returns InvalidArgument.
        /// @param window Open Window whose taskbar state will be published.
        /// @param description Initial presentation and button snapshot.
        /// @return Success, or a status explaining why no taskbar publication was opened.
        [[nodiscard]] IO::Types::Status open(Window &window, const Types::Taskbar::Description &description) noexcept;

        /// @brief Opens with thumbnail-command event delivery through a supplied queue.
        /// @details A non-empty thumbnail-button set requires this overload. The queue is
        /// borrowed until close.
        /// @param window Open Window whose taskbar state will be published.
        /// @param description Initial presentation and button snapshot.
        /// @param eventQueue Open queue owned by the same thread as window.
        /// @return Success, or a status explaining why no taskbar publication was opened.
        [[nodiscard]] IO::Types::Status open(Window &window, const Types::Taskbar::Description &description, ShellEventQueue &eventQueue) noexcept;

        /// @brief Reports whether the taskbar binding is open for its current Window lifetime.
        [[nodiscard]] bool isOpen() const noexcept;

        /// @brief Returns the Window open-lifetime identity retained by this binding.
        /// @return Window identity, or an invalid identity while closed.
        [[nodiscard]] Types::WindowId windowId() const noexcept;

        /// @brief Reports whether the calling thread owns the current binding lifetime.
        [[nodiscard]] bool ownedByCurrentThread() const noexcept;

        /// @brief Reports whether thumbnail-command events have a bound queue.
        [[nodiscard]] bool hasEventQueue() const noexcept;

        /// @brief Closes the binding and releases its taskbar publication.
        /// @return Success when closed or already closed; ResourceBusy for wrong-thread calls or retained native work.
        [[nodiscard]] IO::Types::Status close() noexcept;

        /// @}

        /// @name Presentation updates
        /// @{

        /// @brief Replaces or clears the taskbar progress snapshot.
        /// @details A failed replacement leaves the previously published progress unchanged.
        /// @param progress New normalized progress state, or nullopt to clear progress display.
        /// @return Success, or a validation, ownership, capability, allocation, or native failure.
        [[nodiscard]] IO::Types::Status setProgress(std::optional<Types::Shell::Progress> progress) noexcept;

        /// @brief Replaces or clears the taskbar overlay icon.
        /// @details A failed replacement leaves the previously published overlay unchanged.
        /// @param icon New borrowed RGBA8 icon, or nullopt to clear the overlay.
        /// @return Success, or a validation, ownership, capability, allocation, or native failure.
        [[nodiscard]] IO::Types::Status setOverlayIcon(std::optional<Types::IconImageView> icon) noexcept;

        /// @brief Replaces the complete thumbnail-button set.
        /// @details A non-empty replacement requires a bound event queue; an empty span clears
        /// all buttons. A failed replacement leaves the previously published button set unchanged.
        /// @param buttons Complete borrowed button set; an empty span clears all buttons.
        /// @return Success, or a validation, ownership, queue, capability, size, allocation, or native failure.
        [[nodiscard]] IO::Types::Status setThumbnailButtons(std::span<const Types::Taskbar::ThumbnailButton> buttons) noexcept;

        /// @}

    private:
        friend struct Detail::TaskbarItemAccess;

        std::unique_ptr<Detail::TaskbarItemState> state_;
    };
} // namespace GameWIP::Desktop
