/// @file shell_notifications.h
/// @brief Portable desktop-notification integration for GameWIP Desktop.

#pragma once

#include "desktop/data_transfer.h"
#include "desktop/desktop_export.h"
#include "desktop/shell.h"
#include "desktop/shell_types.h"
#include "io/status.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <variant>

namespace GameWIP::Desktop
{
    namespace Detail
    {
        struct NotificationCenterAccess;
        struct NotificationCenterState;
    } // namespace Detail
} // namespace GameWIP::Desktop

namespace GameWIP::Desktop::Types::Notifications
{
    /// @brief One caller-defined notification action.
    /// @details The action is borrowed until the receiving publish or update operation returns.
    /// Its label and command identity are copied before native publication.
    struct Action
    {
        Shell::CommandId id;    ///< Unique nonzero action identity within one notification.
        std::string_view label; ///< Nonempty UTF-8 action label.
    };

    /// @brief One caller-defined notification text-input field.
    /// @details The input is borrowed until the receiving publish or update operation returns.
    /// Submitted values are delivered through NotificationInputValue events.
    struct Input
    {
        Shell::NotificationInputId id; ///< Unique nonzero input identity within one notification.
        std::string_view placeholder;  ///< Optional UTF-8 placeholder text.
        std::string_view initialValue; ///< Optional initial UTF-8 value.
        bool secure = false;           ///< Whether the platform should obscure entered text.
    };

    /// @brief Borrowed UTF-8 URI notification media source.
    struct UriMediaView
    {
        std::string_view uri; ///< UTF-8 URI identifying media accessible to the platform.
    };

    /// @brief Borrowed notification media source.
    /// @details The image alternative reuses the shared RGBA8 data-transfer image contract.
    using MediaView = std::variant<DataTransfer::ImageView, UriMediaView>;

    /// @brief Notification sound policy.
    enum class Sound : std::uint8_t
    {
        Default, ///< Use the platform's default notification sound.
        Silent   ///< Suppress notification sound playback.
    };

    /// @brief Notification urgency policy.
    enum class Urgency : std::uint8_t
    {
        Low,     ///< Low-priority presentation when the platform supports prioritization.
        Normal,  ///< Normal notification presentation.
        High,    ///< Elevated presentation when the platform supports it.
        Critical ///< Highest requested urgency; the platform may restrict this level.
    };

    /// @brief UTC notification scheduling policy.
    /// @details An occurrence count of one creates a one-time scheduled notification. A larger
    /// finite occurrence count requires a positive repeat interval; unbounded recurrence is not
    /// supported by this API.
    struct Schedule
    {
        std::chrono::sys_time<std::chrono::milliseconds> firstDisplayUtc; ///< First UTC display time.
        std::chrono::milliseconds repeatInterval{};                       ///< Positive interval for recurrence, or zero for one-time delivery.
        std::uint32_t occurrenceCount = 1;                                ///< Total finite occurrence count, including the first display.
    };

    /// @brief Complete borrowed notification content and presentation policy.
    /// @details All strings, media, actions, inputs, and optional progress values are copied before
    /// publish or update returns. Action and input identities must be unique within this snapshot.
    struct Description
    {
        std::string_view title;                  ///< Nonempty UTF-8 notification title.
        std::string_view body;                   ///< Optional UTF-8 notification body.
        std::span<const Action> actions;         ///< Optional caller-defined actions.
        std::span<const Input> inputs;           ///< Optional text-input fields.
        std::span<const MediaView> media;        ///< Optional inline or URI-backed media.
        std::optional<Schedule> schedule;        ///< Optional UTC one-time or finite recurrence schedule.
        std::string_view group;                  ///< Optional UTF-8 grouping key.
        Sound sound = Sound::Default;            ///< Notification sound policy.
        Urgency urgency = Urgency::Normal;       ///< Notification urgency policy.
        std::string_view badge;                  ///< Optional UTF-8 badge or glyph text.
        std::optional<Shell::Progress> progress; ///< Optional normalized notification progress.
    };

    /// @brief Result of publishing one notification.
    struct PublishResult
    {
        IO::Types::Status status; ///< Publication status.
        Shell::NotificationId id; ///< Generated notification identity on success.
    };
} // namespace GameWIP::Desktop::Types::Notifications

namespace GameWIP::Desktop
{
    /// @brief Non-copyable, non-movable owner of process-local notification publication.
    /// @details A successful open binds the center to its owner thread. A supplied
    /// ShellEventQueue is borrowed and fixed for that open lifetime. Notifications without
    /// actions or inputs may be published without a queue; interactive activation, input, and
    /// user-dismissal events require the queue overload. Published identities are valid only
    /// during the owning open lifetime. No callbacks or hidden worker thread are used.
    class DESKTOP_EXPORT NotificationCenter final
    {
    public:
        /// @name Lifecycle
        /// @{

        /// @brief Constructs a closed notification center.
        NotificationCenter() noexcept;
        NotificationCenter(const NotificationCenter &) = delete;
        NotificationCenter &operator=(const NotificationCenter &) = delete;
        NotificationCenter(NotificationCenter &&) = delete;
        NotificationCenter &operator=(NotificationCenter &&) = delete;

        /// @brief Best-effort closes the notification center.
        /// @details The caller must release the center on its owner thread before destruction.
        ~NotificationCenter() noexcept;

        /// @brief Opens notification publication without interactive event delivery.
        /// @return Success, or a status explaining why the center was not opened.
        [[nodiscard]] IO::Types::Status open() noexcept;

        /// @brief Opens notification publication with interactive event delivery.
        /// @param eventQueue Open queue owned by the calling thread and borrowed until close.
        /// @return Success, or a status explaining why the center was not opened.
        [[nodiscard]] IO::Types::Status open(ShellEventQueue &eventQueue) noexcept;

        /// @brief Reports whether the notification center is open.
        [[nodiscard]] bool isOpen() const noexcept;

        /// @brief Reports whether the calling thread owns the current center lifetime.
        [[nodiscard]] bool ownedByCurrentThread() const noexcept;

        /// @brief Reports whether interactive notification events have a bound queue.
        [[nodiscard]] bool hasEventQueue() const noexcept;

        /// @brief Closes notification publication and releases retained native state.
        /// @details Published identities are no longer accepted by this center after close.
        /// @return Success when closed or already closed; ResourceBusy for wrong-thread calls or retained native work.
        [[nodiscard]] IO::Types::Status close() noexcept;

        /// @}

        /// @name Notification operations
        /// @{

        /// @brief Publishes one notification and generates its process-local identity.
        /// @details A description containing actions or inputs requires a queue-bound open lifetime.
        /// A failed publication leaves no usable notification identity.
        /// @param description Borrowed complete notification content and policy.
        /// @return Publication status and generated identity on success.
        [[nodiscard]] Types::Notifications::PublishResult publish(const Types::Notifications::Description &description) noexcept;

        /// @brief Replaces a previously published notification snapshot.
        /// @details The notification identity remains unchanged. A description containing actions
        /// or inputs requires a queue-bound open lifetime. A failed update leaves the prior
        /// notification unchanged.
        /// @param id Published notification identity owned by this center.
        /// @param description Borrowed replacement content and policy.
        /// @return Success, NotFound for an unknown identity, or a validation, ownership, queue, allocation, or native failure.
        [[nodiscard]] IO::Types::Status update(Types::Shell::NotificationId id, const Types::Notifications::Description &description) noexcept;

        /// @brief Dismisses one published notification programmatically.
        /// @param id Published notification identity owned by this center.
        /// @return Success, NotFound for an unknown identity, or an ownership/native failure.
        [[nodiscard]] IO::Types::Status dismiss(Types::Shell::NotificationId id) noexcept;

        /// @}

    private:
        friend struct Detail::NotificationCenterAccess;

        std::unique_ptr<Detail::NotificationCenterState> state_;
    };
} // namespace GameWIP::Desktop
