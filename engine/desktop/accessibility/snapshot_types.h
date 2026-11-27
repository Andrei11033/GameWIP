/// @file snapshot_types.h
/// @brief Portable snapshot, validation, and activation types.

#pragma once

#include "desktop/accessibility/actions.h"
#include "desktop/accessibility/collections.h"
#include "desktop/accessibility/geometry.h"
#include "desktop/accessibility/identity.h"
#include "desktop/accessibility/relations.h"
#include "desktop/accessibility/text.h"

#include "status/result.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace GameWIP::Desktop::Types::Accessibility
{
    /// ------------------------------------------------------------
    /// Semantic nodes and snapshots
    /// ------------------------------------------------------------

    /// @brief One immutable semantic node in a borrowed snapshot view.
    /// @details All strings and spans borrow application storage until addNode()/publish() returns.
    /// Semantic IDs identify application entities, not vector positions; do not reuse an ID for a different entity.
    /// Sensitive/Password state and PasswordField role always hide native values, text, and announcements.
    struct Node
    {
        NodeId id = kInvalidNodeId;     ///< Application-owned nonzero stable identity.
        NodeId parent = kInvalidNodeId; ///< Existing parent identity; zero only for the single root.
        Role role = Role::Unknown;      ///< Semantic role; native types use the closest representable mapping.
        StateSet states;                ///< Current passive state flags; at most one node is Focused.
        ActionSet actions;              ///< Actions accepted by the application's owner-thread handler, not synchronous callbacks.

        std::string_view name;                ///< Borrowed strict UTF-8 accessible label.
        std::string_view description;         ///< Borrowed strict UTF-8 full description.
        std::string_view helpText;            ///< Borrowed strict UTF-8 help text.
        std::string_view value;               ///< Borrowed strict UTF-8 string value, independent of optional numeric/text content.
        std::string_view language;            ///< Borrowed language hint; empty means unspecified; embedded U+0000 is invalid.
        std::string_view accessKey;           ///< Borrowed strict UTF-8 mnemonic description.
        std::string_view keyboardShortcut;    ///< Borrowed strict UTF-8 shortcut description.
        Exposure exposure = Exposure::Normal; ///< Native disclosure policy; portable inspection retains originals.

        std::span<const NodeId> children;                 ///< Borrowed ordered unique children; each child's parent must point back to this node.
        std::span<const Relation> relations;              ///< Borrowed bounded directed semantic relationships.
        std::span<const Extension> extensions;            ///< Borrowed bounded application/vendor extension metadata.
        std::optional<Geometry> geometry;                 ///< Optional transformed local geometry; a geometry-less root uses the client rectangle.
        std::optional<RangeValue> rangeValue;             ///< Optional numeric range-pattern metadata.
        std::optional<ScrollInfo> scroll;                 ///< Optional scroll-pattern metadata.
        std::optional<SelectionInfo> selection;           ///< Optional declared collection selection and text multi-selection policy.
        std::optional<CollectionInfo> collection;         ///< Optional grid dimensions, item coordinates, or semantic set position.
        std::optional<VirtualizationInfo> virtualization; ///< Optional partial-tree/realization metadata.
        std::optional<TextContent> text;                  ///< Optional read-only text document, annotations, selections, and range geometry.
    };

    /// @brief Borrowed complete semantic tree submitted for publication.
    struct SnapshotView
    {
        Generation generation = kInvalidGeneration; ///< Nonzero strictly increasing application publication identity.
        NodeId root = kInvalidNodeId;               ///< The single existing root identity; stable for the enabled lifetime.
        std::span<const Node> nodes;                ///< Borrowed complete connected tree in arbitrary storage order.
    };

    /// ------------------------------------------------------------
    /// Limits, validation, and activation options
    /// ------------------------------------------------------------

    /// @brief Safe limits applied to snapshots, extensions, actions, and notifications.
    struct Limits
    {
        std::uint32_t maximumNodes = 65'536;                 ///< Nodes per complete submitted snapshot.
        std::uint32_t maximumChildrenPerNode = 4'096;        ///< Child identities per node.
        std::uint32_t maximumRelationsPerNode = 256;         ///< Relation records per node, additionally limited to distinct kinds.
        std::uint32_t maximumExtensionsPerNode = 128;        ///< Extension records per node.
        std::uint32_t maximumExtensionListItems = 256;       ///< Values in one extension list.
        std::uint32_t maximumTextBytesPerNode = 1'048'576;   ///< Sum of all UTF-8 field/annotation/extension bytes for one node.
        std::uint32_t maximumTotalTextBytes = 16'777'216;    ///< Sum of all UTF-8 bytes across the complete tree.
        std::uint32_t maximumTextAnnotationsPerNode = 4'096; ///< Text annotations per node.
        std::uint32_t maximumTextFragmentsPerNode = 4'096;   ///< Text geometry fragments per node.
        std::uint32_t maximumActionQueue = 256;              ///< Preallocated transport slots; durable overflow rejects the newest request.
        std::uint32_t maximumNotificationQueue = 512;        ///< Preallocated notification slots; diff overflow collapses to tree invalidation.
        std::uint32_t maximumActionTextBytes = 1'048'576;    ///< Strict UTF-8 bytes copied into one native action.
        std::uint32_t maximumSelectionRangesPerAction = 256; ///< Text ranges in one native action.
        std::uint32_t maximumAnnouncementBytes = 4'096;      ///< Combined announcement text/language bytes.
        std::uint32_t maximumExtensionDepth = 8;             ///< Maximum recursive list depth; valid overrides are in [1, 32].
        std::uint32_t maximumExtensionValuesPerNode = 4'096; ///< Total recursively visited extension values per node.
        std::uint32_t maximumRelationTargetsPerNode = 4'096; ///< Total directed relationship targets per node.
        std::uint32_t maximumTextSelectionsPerNode = 256;    ///< Current text selection ranges per node.
        std::uint32_t maximumNativeProviders = 262'144;      ///< Distinct native node identities per enabled lifetime; at least maximumNodes.
        std::uint32_t maximumNativeTextRanges = 1'024;       ///< Preallocated concurrently retained native text-range slots.
    };

    /// @brief Deterministic reason a borrowed snapshot failed validation.
    enum class ValidationIssue : std::uint8_t
    {
        None,
        InvalidLimits,
        ZeroGeneration,
        EmptyTree,
        InvalidRoot,
        InvalidNodeId,
        DuplicateNodeId,
        MissingNode,
        ParentMismatch,
        DuplicateChild,
        Cycle,
        UnreachableNode,
        InvalidRole,
        InvalidAction,
        InvalidRelation,
        InvalidText,
        InvalidTextRange,
        InvalidGeometry,
        InvalidValue,
        InvalidExtension,
        InvalidState,
        InvalidSelection,
        InvalidCollection,
        InvalidVirtualization,
        LimitExceeded
    };

    /// @brief Validation details retained for success or a semantic validation failure.
    struct ValidationInfo
    {
        ValidationIssue issue = ValidationIssue::None; ///< First deterministic semantic issue, or None when valid.
        NodeId node = kInvalidNodeId;                  ///< Offending node when known.
        std::size_t index = 0;                         ///< Offending node or bounded field index, depending on issue.
    };

    /// @brief Shared result carrying validation details on success or semantic failure.
    using ValidationResult = GameWIP::Status::Types::Result<ValidationInfo>;

    /// @brief Summary of the currently published immutable snapshot.
    struct SnapshotInfo
    {
        bool available = false;                     ///< Whether this summary describes a retained published tree.
        Generation generation = kInvalidGeneration; ///< Retained immutable generation, or zero when absent.
        NodeId root = kInvalidNodeId;               ///< Retained root identity, or zero when absent.
        std::uint32_t nodeCount = 0;                ///< Number of retained semantic nodes.
    };

    /// @brief Configuration for one explicit per-open activation.
    struct Options
    {
        Limits limits; ///< Positive bounded capacities used for this explicit activation.
    };
} // namespace GameWIP::Desktop::Types::Accessibility
