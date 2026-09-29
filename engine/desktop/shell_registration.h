/// @file shell_registration.h
/// @brief Portable current-user shell file and URI registration for GameWIP Desktop.

#pragma once

#include "desktop/desktop_export.h"
#include "desktop/shell_types.h"
#include "io/status.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace GameWIP::Desktop::Types::Registration
{
    /// @brief Conflict handling policy for one current-user registration operation.
    enum class ConflictPolicy : std::uint8_t
    {
        Report,      ///< Reject any existing target conflict without writing and report it.
        Coexist,     ///< Install or update this owner without changing foreign ownership and report foreign conflicts.
        ReplaceOwned ///< Replace a same-owner registration atomically; reject foreign conflicts.
    };

    /// @brief Kind of shell registration target reported by a conflict.
    enum class TargetKind : std::uint8_t
    {
        FileExtension, ///< Filesystem extension target such as .gamewip.
        UriScheme      ///< URI scheme target such as gamewip.
    };

    /// @brief One reported pre-existing shell registration conflict.
    /// @details Conflict text is materialized by the registration operation and remains valid in
    /// the owning result.
    struct Conflict
    {
        TargetKind kind = TargetKind::FileExtension; ///< Conflicting target kind.
        std::string target;                          ///< Owned normalized extension or URI scheme.
        std::string ownerKey;                        ///< Owned existing owner identity when available.
    };

    /// @brief Result of one registration or unregistration operation.
    struct Result
    {
        IO::Types::Status status;        ///< Operation status.
        std::vector<Conflict> conflicts; ///< Reported foreign or otherwise incompatible conflicts.
        bool replacedOwned = false;      ///< Whether an existing same-owner registration was replaced.
    };

    /// @brief One caller-defined shell context-menu verb.
    /// @details The verb name, label, and structured launch action are borrowed until the receiving
    /// registration operation returns. Launch placeholders expand from the shell-selected target set.
    struct Verb
    {
        std::string_view name;          ///< Nonempty canonical verb name.
        std::string_view label;         ///< Nonempty UTF-8 display label.
        Shell::LaunchActionView action; ///< Structured executable and argument launch action.
    };

    /// @brief Optional shell icon metadata for one registration descriptor.
    struct Icon
    {
        Shell::PathTargetView path; ///< Borrowed filesystem path containing the icon resource.
        std::int32_t index = 0;     ///< Icon index within the resource file.
    };

    /// @brief Reusable current-user file-extension registration descriptor.
    /// @details The owner key identifies registrations created by this application. The extension
    /// is normalized without a leading wildcard and normally begins with a period.
    struct FileExtensionDescription
    {
        std::string_view ownerKey;                               ///< Stable nonempty registration owner key.
        std::string_view extension;                              ///< Nonempty extension such as .gamewip.
        std::string_view displayName;                            ///< Nonempty UTF-8 shell display name.
        std::string_view contentType;                            ///< Optional UTF-8 MIME/content-type metadata.
        std::optional<Icon> icon;                                ///< Optional borrowed icon metadata.
        std::optional<Shell::LaunchActionView> defaultAction;    ///< Optional default/open launch action.
        std::span<const Verb> verbs;                             ///< Optional context-menu verbs.
        ConflictPolicy conflictPolicy = ConflictPolicy::Coexist; ///< Conflict handling policy.
    };

    /// @brief Reusable current-user URI-scheme registration descriptor.
    /// @details The scheme is normalized without a URI separator or wildcard. URI launch actions
    /// may use target placeholders to receive the selected URI.
    struct UriSchemeDescription
    {
        std::string_view ownerKey;                               ///< Stable nonempty registration owner key.
        std::string_view scheme;                                 ///< Nonempty URI scheme without a trailing colon.
        std::string_view displayName;                            ///< Nonempty UTF-8 shell display name.
        std::optional<Icon> icon;                                ///< Optional borrowed icon metadata.
        std::optional<Shell::LaunchActionView> defaultAction;    ///< Optional default/open launch action.
        std::span<const Verb> verbs;                             ///< Optional context-menu verbs.
        ConflictPolicy conflictPolicy = ConflictPolicy::Coexist; ///< Conflict handling policy.
    };
} // namespace GameWIP::Desktop::Types::Registration

/// @brief Stateless current-user shell registration operations.
namespace GameWIP::Desktop::Registration
{
    /// @brief Atomically registers or replaces one current-user file-extension association.
    /// @details Registration is scoped to the current user and never forces the default
    /// application. All descriptor views are copied before the operation returns. A failed
    /// replacement leaves the prior same-owner registration unchanged.
    /// @param description Reusable file-extension registration descriptor.
    /// @return Operation status, conflict report, and replacement information.
    [[nodiscard]] DESKTOP_EXPORT Types::Registration::Result registerFileExtension(
        const Types::Registration::FileExtensionDescription &description) noexcept;

    /// @brief Atomically registers or replaces one current-user URI-scheme association.
    /// @details Registration is scoped to the current user and never forces the default
    /// application. All descriptor views are copied before the operation returns. A failed
    /// replacement leaves the prior same-owner registration unchanged.
    /// @param description Reusable URI-scheme registration descriptor.
    /// @return Operation status, conflict report, and replacement information.
    [[nodiscard]] DESKTOP_EXPORT Types::Registration::Result registerUriScheme(const Types::Registration::UriSchemeDescription &description) noexcept;

    /// @brief Removes one current-user file-extension registration owned by the supplied key.
    /// @param ownerKey Stable nonempty owner key used when the registration was created.
    /// @param extension Extension without a wildcard.
    /// @return Operation status and any ownership conflict report.
    [[nodiscard]] DESKTOP_EXPORT Types::Registration::Result unregisterFileExtension(std::string_view ownerKey, std::string_view extension) noexcept;

    /// @brief Removes one current-user URI-scheme registration owned by the supplied key.
    /// @param ownerKey Stable nonempty owner key used when the registration was created.
    /// @param scheme URI scheme without a trailing colon.
    /// @return Operation status and any ownership conflict report.
    [[nodiscard]] DESKTOP_EXPORT Types::Registration::Result unregisterUriScheme(std::string_view ownerKey, std::string_view scheme) noexcept;
} // namespace GameWIP::Desktop::Registration
