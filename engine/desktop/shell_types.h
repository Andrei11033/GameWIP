/// @file shell_types.h
/// @brief Shared portable value vocabulary for GameWIP Desktop shell integration.

#pragma once

#include "desktop/types.h"
#include "filesystem/path.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

/// @brief Portable shell identities, targets, launch values, progress, and event payloads.
namespace GameWIP::Desktop::Types::Shell
{
    /// @brief Process-local identity of one registered tray icon.
    struct TrayIconId
    {
        std::uint64_t value = 0; ///< Opaque generated identity; zero represents no tray icon.

        /// @brief Reports whether this tray-icon identity is usable.
        [[nodiscard]] constexpr bool isValid() const noexcept
        {
            return value != 0;
        }

        friend constexpr bool operator==(TrayIconId, TrayIconId) noexcept = default;
    };

    /// @brief Process-local identity of one published notification.
    struct NotificationId
    {
        std::uint64_t value = 0; ///< Opaque generated identity; zero represents no notification.

        /// @brief Reports whether this notification identity is usable.
        [[nodiscard]] constexpr bool isValid() const noexcept
        {
            return value != 0;
        }

        friend constexpr bool operator==(NotificationId, NotificationId) noexcept = default;
    };

    /// @brief Caller-defined identity of one shell command within its owning resource.
    struct CommandId
    {
        std::uint32_t value = 0; ///< Caller-defined identity; zero is invalid.

        /// @brief Reports whether this command identity is usable.
        [[nodiscard]] constexpr bool isValid() const noexcept
        {
            return value != 0;
        }

        friend constexpr bool operator==(CommandId, CommandId) noexcept = default;
    };

    /// @brief Non-owning filesystem-path target used by a call-scoped shell description.
    /// @details The referenced path must remain alive until the receiving operation returns.
    struct PathTargetView
    {
        std::reference_wrapper<const FileSystem::Types::Path> path; ///< Borrowed path spelling.
    };

    /// @brief Non-owning UTF-8 URI target used by a call-scoped shell description.
    /// @details The referenced URI storage must remain alive until the receiving operation returns.
    struct UriTargetView
    {
        std::string_view uri; ///< Borrowed UTF-8 URI text.
    };

    /// @brief Tagged non-owning filesystem-path or URI target.
    /// @details The alternatives are @c std::monostate for no target, @c PathTargetView for
    /// filesystem-path semantics, and @c UriTargetView for URI semantics.
    using TargetView = std::variant<std::monostate, PathTargetView, UriTargetView>;

    /// @brief Owning filesystem-path target.
    struct PathTarget
    {
        FileSystem::Types::Path path; ///< Owned filesystem-path spelling.

        friend bool operator==(PathTarget, PathTarget) noexcept = default;
    };

    /// @brief Owning UTF-8 URI target.
    struct UriTarget
    {
        std::string uri; ///< Owned UTF-8 URI text.

        friend bool operator==(UriTarget, UriTarget) noexcept = default;
    };

    /// @brief Tagged owning filesystem-path or URI target.
    /// @details The alternatives are @c std::monostate for no target, @c PathTarget for
    /// filesystem-path semantics, and @c UriTarget for URI semantics.
    using Target = std::variant<std::monostate, PathTarget, UriTarget>;

    /// @brief Placeholder for target substitution in a launch argument.
    /// @details Registration commands expand these placeholders when their shell event supplies
    /// the selected target set. Jump-list launch actions reject placeholder arguments because
    /// they have no selection context.
    enum class LaunchPlaceholder : std::uint8_t
    {
        SelectedPath, ///< Substitute the selected filesystem path.
        SelectedUri,  ///< Substitute the selected URI.
        AllPaths,     ///< Substitute all filesystem paths.
        AllUris       ///< Substitute all URIs.
    };

    /// @brief Non-owning literal launch argument for a call-scoped action.
    /// @details The referenced text must remain alive until the receiving operation returns.
    struct LiteralLaunchArgumentView
    {
        std::string_view text; ///< Borrowed UTF-8 argument text.
    };

    /// @brief Non-owning target launch argument for a call-scoped action.
    /// @details Referenced target storage must remain alive until the receiving operation returns.
    struct TargetLaunchArgumentView
    {
        TargetView target; ///< Borrowed target value.
    };

    /// @brief Tagged non-owning launch argument.
    using LaunchArgumentView = std::variant<LiteralLaunchArgumentView, TargetLaunchArgumentView, LaunchPlaceholder>;

    /// @brief Owning literal launch argument.
    struct LiteralLaunchArgument
    {
        std::string text; ///< Owned UTF-8 argument text.

        friend bool operator==(LiteralLaunchArgument, LiteralLaunchArgument) noexcept = default;
    };

    /// @brief Owning target launch argument.
    struct TargetLaunchArgument
    {
        Target target; ///< Owned target value.

        friend bool operator==(TargetLaunchArgument, TargetLaunchArgument) noexcept = default;
    };

    /// @brief Tagged owning launch argument.
    using LaunchArgument = std::variant<LiteralLaunchArgument, TargetLaunchArgument, LaunchPlaceholder>;

    /// @brief Non-owning executable and arguments for a call-scoped launch action.
    /// @details The executable and argument storage must remain alive until the receiving operation returns.
    struct LaunchActionView
    {
        PathTargetView executable;                     ///< Borrowed executable path.
        std::span<const LaunchArgumentView> arguments; ///< Borrowed ordered arguments.
    };

    /// @brief Owning executable and arguments for a shell launch action.
    struct LaunchAction
    {
        PathTarget executable;                 ///< Owned executable path.
        std::vector<LaunchArgument> arguments; ///< Owned ordered arguments.

        friend bool operator==(LaunchAction, LaunchAction) noexcept = default;
    };

    /// @brief Progress display state for a shell resource.
    enum class ProgressState : std::uint8_t
    {
        Normal,        ///< Determinate progress.
        Indeterminate, ///< Progress is ongoing without a known fraction.
        Paused,        ///< Progress is temporarily paused.
        Error          ///< Progress failed.
    };

    /// @brief Progress display state and normalized completion fraction.
    struct Progress
    {
        ProgressState state = ProgressState::Normal; ///< Current progress state.
        double fraction = 0.0;                       ///< Valid for determinate states in [0, 1]; ignored for Indeterminate.

        friend bool operator==(Progress, Progress) noexcept = default;
    };

    /// @brief Caller-defined identity of one notification input.
    struct NotificationInputId
    {
        std::uint32_t value = 0; ///< Caller-defined identity; zero is invalid.

        /// @brief Reports whether this notification-input identity is usable.
        [[nodiscard]] constexpr bool isValid() const noexcept
        {
            return value != 0;
        }

        friend constexpr bool operator==(NotificationInputId, NotificationInputId) noexcept = default;
    };

    /// @brief Portable shell event payloads.
    namespace Events
    {
        /// @brief User interaction that activated a tray icon.
        enum class TrayActivationKind : std::uint8_t
        {
            Primary,      ///< Primary activation, typically a left click.
            Secondary,    ///< Secondary activation, typically a right click.
            DoublePrimary ///< Double primary activation.
        };

        /// @brief Reports user interaction with a tray icon.
        struct TrayActivated
        {
            TrayIconId iconId;                                     ///< Activated tray icon.
            TrayActivationKind kind = TrayActivationKind::Primary; ///< Activation kind.
            std::optional<ScreenPosition> screenPosition;          ///< Screen position when supplied by the platform.
        };

        /// @brief Reports invocation of a tray command.
        struct TrayCommandInvoked
        {
            TrayIconId iconId;   ///< Tray icon that owns the command.
            CommandId commandId; ///< Invoked command.
        };

        /// @brief Reports invocation of a taskbar thumbnail command.
        struct TaskbarThumbnailButtonInvoked
        {
            WindowId windowId;   ///< Window whose thumbnail was activated.
            CommandId commandId; ///< Invoked command.
        };

        /// @brief Reports one value submitted with a notification activation.
        struct NotificationInputValue
        {
            NotificationInputId inputId; ///< Submitted input identity.
            std::string value;           ///< Owned UTF-8 input value.
        };

        /// @brief Reason a published notification was dismissed.
        enum class NotificationDismissReason : std::uint8_t
        {
            UserDismissed, ///< The user dismissed the notification.
            Expired,       ///< The notification reached its display timeout.
            Replaced,      ///< A newer notification replaced it.
            Programmatic,  ///< The application requested dismissal.
            Platform,      ///< The platform dismissed it for another known reason.
            Unknown        ///< The platform did not provide a reason.
        };

        /// @brief Reports activation of a published notification.
        struct NotificationActivated
        {
            NotificationId notificationId;                   ///< Activated notification.
            std::optional<CommandId> actionId;               ///< Activated action when supplied by the platform.
            std::vector<NotificationInputValue> inputValues; ///< Submitted input values.
        };

        /// @brief Reports dismissal of a published notification.
        struct NotificationDismissed
        {
            NotificationId notificationId;                                         ///< Dismissed notification.
            NotificationDismissReason reason = NotificationDismissReason::Unknown; ///< Dismissal reason.
        };

        /// @brief Reason a shell-owned native resource was lost.
        enum class ResourceLossReason : std::uint8_t
        {
            NativeDestroyed, ///< The native resource was destroyed externally.
            UserRemoved,     ///< The user removed the resource.
            BackendReset,    ///< The backend reset its shell state.
            BackendFailure,  ///< The backend reported a resource failure.
            Unknown          ///< The loss reason is unavailable.
        };

        /// @brief Reports that a tray icon's native state was lost.
        struct TrayIconStateLost
        {
            TrayIconId iconId;                                       ///< Tray icon whose native state was lost.
            ResourceLossReason reason = ResourceLossReason::Unknown; ///< Resource-loss reason.
            std::int64_t nativeCode = 0;                             ///< Platform-native diagnostic code when available.
            std::string message;                                     ///< Owned diagnostic message when available.
        };

        /// @brief Tagged payload for one portable shell event.
        using Payload = std::variant<
            TrayActivated,
            TrayCommandInvoked,
            TaskbarThumbnailButtonInvoked,
            NotificationActivated,
            NotificationDismissed,
            TrayIconStateLost>;
    } // namespace Events

    /// @brief One queued shell event with a per-open monotonic sequence.
    struct Event
    {
        std::uint64_t sequence = 0; ///< Monotonic sequence within the current open lifetime.
        Events::Payload data;       ///< Tagged shell event payload.

        /// @brief Queries the mutable event payload by type.
        /// @tparam PayloadType Payload alternative to query.
        /// @return Pointer to the stored payload, or nullptr if the type does not match.
        template <typename PayloadType> [[nodiscard]] PayloadType *getIf() noexcept
        {
            return std::get_if<PayloadType>(&data);
        }

        /// @brief Queries the const event payload by type.
        /// @tparam PayloadType Payload alternative to query.
        /// @return Pointer to the stored payload, or nullptr if the type does not match.
        template <typename PayloadType> [[nodiscard]] const PayloadType *getIf() const noexcept
        {
            return std::get_if<PayloadType>(&data);
        }
    };

} // namespace GameWIP::Desktop::Types::Shell
