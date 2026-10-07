/// @file accessibility.h
/// @brief Portable semantic accessibility snapshot bridge for custom-rendered Window content.
/// @details This header contains no platform-native types and is not a wire-protocol definition.

#pragma once

#include "desktop/desktop_export.h"
#include "desktop/window.h"
#include "io/status.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace GameWIP::Desktop::Detail
{
    struct PublishedSnapshot;
    struct SnapshotBuilderState;
} // namespace GameWIP::Desktop::Detail

namespace GameWIP::Desktop::Types::Accessibility
{
    // ------------------------------------------------------------
    // Identity, roles, state, and capabilities
    // ------------------------------------------------------------

    /// @brief Stable application-owned identity for one semantic node in an open Window lifetime.
    using NodeId = std::uint64_t;

    /// @brief Strictly increasing identity assigned to one published semantic snapshot.
    using Generation = std::uint64_t;

    /// @brief Identity assigned to one native accessibility action request.
    using RequestId = std::uint64_t;

    inline constexpr NodeId kInvalidNodeId = 0;         ///< Reserved absent-node identity.
    inline constexpr Generation kInvalidGeneration = 0; ///< Reserved unpublished generation.
    inline constexpr RequestId kInvalidRequestId = 0;   ///< Reserved unaccepted request identity.

    /// @brief Portable semantic roles understood by the bridge.
    enum class Role : std::uint16_t
    {
        Unknown,
        Application,
        Window,
        Dialog,
        Group,
        Pane,
        Section,
        Toolbar,
        MenuBar,
        Menu,
        MenuItem,
        CheckBox,
        RadioButton,
        Button,
        ToggleButton,
        Link,
        Image,
        StaticText,
        TextField,
        PasswordField,
        SearchField,
        Slider,
        ProgressBar,
        ScrollBar,
        ScrollArea,
        List,
        ListItem,
        Tree,
        TreeItem,
        Table,
        Row,
        Cell,
        ColumnHeader,
        RowHeader,
        Tab,
        TabList,
        TabPanel,
        ComboBox,
        SpinButton,
        Calendar,
        DateInput,
        TimeInput,
        Separator,
        Status,
        Alert,
        Tooltip,
        Video,
        Audio,
        Canvas,
        Document,
        Article,
        Navigation,
        Form,
        Landmark,
        Custom,
        Count
    };

    /// @brief State flags describing a semantic node.
    enum class State : std::uint64_t
    {
        Disabled = std::uint64_t{1} << 0,
        Focusable = std::uint64_t{1} << 1,
        Focused = std::uint64_t{1} << 2,
        Offscreen = std::uint64_t{1} << 3,
        Invisible = std::uint64_t{1} << 4,
        ReadOnly = std::uint64_t{1} << 5,
        Required = std::uint64_t{1} << 6,
        Selected = std::uint64_t{1} << 7,
        Selectable = std::uint64_t{1} << 8,
        Expanded = std::uint64_t{1} << 9,
        Expandable = std::uint64_t{1} << 10,
        Checked = std::uint64_t{1} << 11,
        Mixed = std::uint64_t{1} << 12,
        Pressed = std::uint64_t{1} << 13,
        Password = std::uint64_t{1} << 14,
        Multiline = std::uint64_t{1} << 15,
        HasPopup = std::uint64_t{1} << 16,
        Modal = std::uint64_t{1} << 17,
        Busy = std::uint64_t{1} << 18,
        Visited = std::uint64_t{1} << 19,
        Invalid = std::uint64_t{1} << 20,
        Editable = std::uint64_t{1} << 21,
        Sensitive = std::uint64_t{1} << 22
    };

    /// @brief Packed state flags with no ownership or allocation.
    struct StateSet
    {
        std::uint64_t flags = 0; ///< Bitwise combination of State enumerators; unknown bits are invalid.

        /// @brief Tests one State flag without allocation.
        [[nodiscard]] constexpr bool contains(State state) const noexcept
        {
            return (flags & static_cast<std::uint64_t>(state)) != 0;
        }
    };

    /// @brief Standard actions a node may accept from a native accessibility client.
    /// @details The bounded queue may coalesce only Focus, SetValue, ScrollTo, and Realize
    /// requests for the same node. Invoke, Toggle, selection mutation, increment/decrement,
    /// ScrollBy, and SetTextSelection requests retain FIFO ordering.
    enum class ActionKind : std::uint8_t
    {
        Focus,
        Invoke,
        SetValue,
        Increment,
        Decrement,
        Toggle,
        Expand,
        Collapse,
        Select,
        Deselect,
        ScrollTo,
        ScrollBy,
        SetTextSelection,
        Realize,
        Count
    };

    /// @brief Packed supported-action flags.
    struct ActionSet
    {
        std::uint64_t flags = 0; ///< Bit i advertises ActionKind i; unknown bits are invalid.

        /// @brief Tests one valid action enumerator; invalid enumerators return false.
        [[nodiscard]] constexpr bool contains(ActionKind action) const noexcept
        {
            const auto index = static_cast<std::uint8_t>(action);
            return index < static_cast<std::uint8_t>(ActionKind::Count) && (flags & (std::uint64_t{1} << index)) != 0;
        }
    };

    /// @brief Optional bridge capabilities reported independently of the global Window capability.
    enum class Feature : std::uint8_t
    {
        Snapshot,
        NativeTree,
        Geometry,
        ReadOnlyText,
        Actions,
        Notifications,
        Announcements,
        Virtualization,
        Values,
        RangeValues,
        Toggle,
        ExpandCollapse,
        Selection,
        Scrolling,
        Collections,
        Relations,
        TextAnnotations,
        TextRangeGeometry,
        AnnouncementLanguage, ///< Native support for overriding the source node's language for one announcement.
        Count
    };

    /// @brief Packed optional bridge capabilities.
    struct Features
    {
        std::uint64_t flags = 0; ///< Bit i reports implemented Feature i; missing bits indicate unsupported semantics.

        /// @brief Tests one valid feature enumerator; invalid enumerators return false.
        [[nodiscard]] constexpr bool supports(Feature feature) const noexcept
        {
            const auto index = static_cast<std::uint8_t>(feature);
            return index < static_cast<std::uint8_t>(Feature::Count) && (flags & (std::uint64_t{1} << index)) != 0;
        }
    };

    /// @brief Controls which semantic content may be exposed to native clients.
    enum class Exposure : std::uint8_t
    {
        Normal,       ///< Exposes submitted semantic metadata normally.
        RedactValue,  ///< Hides string and numeric native values and value patterns.
        RedactText,   ///< Hides native text patterns and text-derived annotation associations.
        RedactContent ///< Hides descriptive strings, values, text, and announcements; preserves structure and bounds.
    };

    // ------------------------------------------------------------
    // Geometry
    // ------------------------------------------------------------

    /// @brief Floating-point point in Window client logical coordinates.
    struct Point
    {
        double x = 0.0; ///< Horizontal coordinate in the operation's documented logical space.
        double y = 0.0; ///< Vertical coordinate in the operation's documented logical space.
    };

    /// @brief Floating-point rectangle in a node's local logical coordinates.
    struct Rect
    {
        double x = 0.0;      ///< Left edge in the containing field's documented logical space.
        double y = 0.0;      ///< Top edge in the containing field's documented logical space.
        double width = 0.0;  ///< Finite nonnegative logical width.
        double height = 0.0; ///< Finite nonnegative logical height.
    };

    /// @brief Two-dimensional affine transform from node-local coordinates to Window client coordinates.
    struct AffineTransform
    {
        double m11 = 1.0; ///< Local x contribution to Window x.
        double m12 = 0.0; ///< Local x contribution to Window y.
        double m21 = 0.0; ///< Local y contribution to Window x.
        double m22 = 1.0; ///< Local y contribution to Window y.
        double tx = 0.0;  ///< Window-client logical x translation.
        double ty = 0.0;  ///< Window-client logical y translation.
    };

    /// @brief Optional node geometry and hit-testing metadata.
    struct Geometry
    {
        Rect localBounds;                   ///< Rectangle in this node's local logical coordinates.
        AffineTransform toWindow;           ///< Direct local-to-Window transform, not relative to the parent.
        std::optional<Rect> clippingBounds; ///< Window-client logical clip inherited by descendants.
        bool visible = true;                ///< Combines with ancestor and Window visibility for native exposure.
        bool hitTestable = true;            ///< Requires an invertible transform when true.
        std::uint32_t hitTestOrder = 0;     ///< Higher orders win overlap; ties use later semantic preorder.
    };

    // ------------------------------------------------------------
    // Text content and layout
    // ------------------------------------------------------------

    /// @brief UTF-8 byte range. End is exclusive.
    /// @details Ranges must begin and end at documented Unicode scalar and grapheme boundaries.
    struct TextRange
    {
        std::uint32_t begin = 0; ///< Inclusive UTF-8 byte offset at a grapheme boundary.
        std::uint32_t end = 0;   ///< Exclusive UTF-8 byte offset at a grapheme boundary; begin == end is a caret.
    };

    /// @brief Text reading direction.
    enum class TextDirection : std::uint8_t
    {
        Unspecified,
        LeftToRight,
        RightToLeft,
        Mixed
    };

    /// @brief Read-only text annotation kinds.
    enum class TextAnnotationKind : std::uint8_t
    {
        Language,
        Emphasis,
        Spelling,
        Link,
        Custom
    };

    /// @brief Annotation applied to one UTF-8 text range.
    struct TextAnnotation
    {
        TextRange range;                                      ///< Annotated UTF-8 byte range in the containing TextContent.
        TextAnnotationKind kind = TextAnnotationKind::Custom; ///< Typed annotation category.
        std::string_view value;                               ///< Borrowed strict UTF-8; language values reject embedded U+0000.
        NodeId target = kInvalidNodeId;                       ///< Optional semantic annotation/link node; nonzero references must exist.
    };

    /// @brief Optional geometry for one fragment of read-only text.
    struct TextFragment
    {
        TextRange range; ///< UTF-8 byte range covered by this application-supplied layout fragment.
        Rect bounds;     ///< Window-client logical bounds for the represented text fragment.
    };

    /// @brief Read-only UTF-8 text plus ranges, annotations, direction, and language metadata.
    struct TextContent
    {
        std::string_view utf8;                                ///< Borrowed strict UTF-8; embedded U+0000 is preserved.
        TextDirection direction = TextDirection::Unspecified; ///< Reading direction, not an inferred rendering layout.
        std::string_view language;                            ///< Borrowed language hint; empty inherits Node language; embedded U+0000 is invalid.
        std::span<const TextAnnotation> annotations;          ///< Borrowed bounded annotations; overlapping annotations are permitted.
        std::span<const TextFragment> fragments;              ///< Borrowed optional Window-client logical range geometry.
        std::span<const TextRange> selection;                 ///< Borrowed current text selections in UTF-8 byte offsets.
        std::optional<std::uint32_t> caret;                   ///< Optional current caret at a grapheme boundary.
    };

    // ------------------------------------------------------------
    // Values, selection, and collections
    // ------------------------------------------------------------

    /// @brief Numeric range metadata for sliders, progress values, and similar controls.
    struct RangeValue
    {
        double value = 0.0;       ///< Finite current value between minimum and maximum.
        double minimum = 0.0;     ///< Finite inclusive lower bound.
        double maximum = 0.0;     ///< Finite inclusive upper bound.
        double smallChange = 0.0; ///< Finite nonnegative small-step magnitude in the application's numeric units.
        double largeChange = 0.0; ///< Finite nonnegative large-step magnitude in the application's numeric units.
    };

    /// @brief Scroll state exposed by scrollable nodes.
    struct ScrollInfo
    {
        bool horizontalScrollable = false; ///< Whether horizontal scrolling is currently meaningful.
        bool verticalScrollable = false;   ///< Whether vertical scrolling is currently meaningful.
        double horizontalPercent = 0.0;    ///< Horizontal destination percentage in [0, 100].
        double verticalPercent = 0.0;      ///< Vertical destination percentage in [0, 100].
        double horizontalViewSize = 100.0; ///< Visible horizontal extent as a percentage in [0, 100].
        double verticalViewSize = 100.0;   ///< Visible vertical extent as a percentage in [0, 100].
    };

    /// @brief Selection state for list, tree, table, and text nodes.
    struct SelectionInfo
    {
        std::span<const NodeId> selectedNodes; ///< Borrowed distinct Selectable/Selected descendants, in application selection order.
        NodeId activeNode = kInvalidNodeId;    ///< Optional active identity, required to occur in selectedNodes when nonzero.
        bool multiSelectable = false;          ///< Allows more than one selected node and multiple native text selections.
        bool selectionRequired = false;        ///< Requires at least one selected node in this declared collection selection.
    };

    /// @brief Collection position and table dimensions.
    struct CollectionInfo
    {
        std::uint32_t rowCount = 0;      ///< Grid rows; zero represents an empty grid or unspecified item-local dimensions.
        std::uint32_t columnCount = 0;   ///< Grid columns; zero represents an empty grid or unspecified item-local dimensions.
        std::uint32_t rowIndex = 0;      ///< Zero-based item row, bounded by its nearest declared Table.
        std::uint32_t columnIndex = 0;   ///< Zero-based item column, bounded by its nearest declared Table.
        std::uint32_t rowSpan = 1;       ///< Positive row span within the declared grid.
        std::uint32_t columnSpan = 1;    ///< Positive column span within the declared grid.
        std::uint32_t positionInSet = 0; ///< One-based semantic set position; zero means unspecified.
        std::uint32_t setSize = 0;       ///< Semantic set size; zero means unspecified.
        std::uint32_t level = 0;         ///< One-based hierarchy level; zero means unspecified.
    };

    /// @brief Realization and partial-tree metadata for virtualized collections.
    struct VirtualizationInfo
    {
        bool virtualized = false;             ///< Whether this node participates in a partial virtualized tree.
        bool realized = true;                 ///< Whether the submitted node is currently realized by the application.
        bool canRealize = false;              ///< Requires a supported Realize action; does not install a lazy callback.
        std::uint32_t realizedChildCount = 0; ///< Realized known children, no greater than children.size() or totalChildCount.
        std::uint32_t totalChildCount = 0;    ///< Logical collection child count including items absent from the snapshot.
    };

    // ------------------------------------------------------------
    // Relations and extensions
    // ------------------------------------------------------------

    /// @brief Relationship kinds between semantic nodes.
    enum class RelationKind : std::uint8_t
    {
        LabelledBy,
        DescribedBy,
        ControllerFor,
        ControlledBy,
        FlowsTo,
        FlowsFrom,
        Details,
        ErrorMessage,
        MemberOf,
        RowHeaders,
        ColumnHeaders
    };

    /// @brief One directed relationship from a node to one or more other nodes.
    struct Relation
    {
        RelationKind kind = RelationKind::LabelledBy; ///< At most one relation of each kind per node.
        std::span<const NodeId> targets;              ///< Borrowed nonempty distinct existing-node targets, preserving application order.
    };

    /// @brief Bounded typed value for a namespaced extension property.
    enum class ExtensionValueKind : std::uint8_t
    {
        Boolean,
        SignedInteger,
        UnsignedInteger,
        Double,
        Utf8,
        NodeReference,
        List
    };

    /// @brief Allocation-free view of a supported extension value.
    struct ExtensionValue
    {
        ExtensionValueKind kind = ExtensionValueKind::Utf8; ///< Selects the sole active payload; inactive fields are ignored and not copied.
        bool booleanValue = false;                          ///< Boolean payload.
        std::int64_t signedValue = 0;                       ///< Signed-integer payload.
        std::uint64_t unsignedValue = 0;                    ///< Unsigned-integer payload.
        double doubleValue = 0.0;                           ///< Finite floating-point payload.
        NodeId nodeValue = kInvalidNodeId;                  ///< Existing nonzero semantic node-reference payload.
        std::string_view utf8Value;                         ///< Borrowed strict UTF-8 payload.
        std::span<const ExtensionValue> listValue;          ///< Borrowed bounded recursively typed list payload.
    };

    /// @brief One namespaced, bounded extension property.
    struct Extension
    {
        std::string_view namespaceName; ///< Nonempty strict UTF-8 namespace owned by the application/vendor.
        std::string_view name;          ///< Nonempty strict UTF-8 key; namespace/key pairs are unique per node.
        ExtensionValue value;           ///< Bounded typed portable payload; native backends need not register custom properties.
    };

    // ------------------------------------------------------------
    // Semantic nodes and snapshots
    // ------------------------------------------------------------

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

    // ------------------------------------------------------------
    // Limits, validation, and activation options
    // ------------------------------------------------------------

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

    /// @brief Rich result from portable snapshot validation.
    struct ValidationResult
    {
        IO::Types::Status status;                      ///< Success, InvalidArgument, SizeLimitExceeded, or implementation allocation failure.
        ValidationIssue issue = ValidationIssue::None; ///< First deterministic semantic issue, or None for resource failures.
        NodeId node = kInvalidNodeId;                  ///< Offending node when known.
        std::size_t index = 0;                         ///< Offending node or bounded field index, depending on issue.

        /// @brief Reports whether validation completed successfully.
        [[nodiscard]] constexpr bool ok() const noexcept
        {
            return status.ok();
        }
    };

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

    // ------------------------------------------------------------
    // Actions and notifications
    // ------------------------------------------------------------

    /// @brief Transport outcome when a native client submits an action request.
    enum class ActionAcceptance : std::uint8_t
    {
        Accepted,
        Rejected,
        Closed,
        Invalid,
        Unsupported,
        QueueFull
    };

    /// @brief Transport-only action submission result; it does not report application execution.
    struct ActionResult
    {
        ActionAcceptance acceptance = ActionAcceptance::Rejected; ///< Transport outcome, never an application execution result.
        RequestId requestId = kInvalidRequestId;                  ///< Unique accepted request identity; zero on rejection.
        Generation observedGeneration = kInvalidGeneration;       ///< Immutable generation queried by the native client.
    };

    /// @brief Coarse notification categories generated by publication diffs.
    enum class NotificationKind : std::uint8_t
    {
        TreeInvalidated,
        StructureChanged,
        PropertyChanged,
        FocusChanged,
        SelectionChanged,
        TextChanged,
        LiveRegionChanged
    };

    /// @brief Properties that may be reported by a property-change notification.
    enum class PropertyKind : std::uint8_t
    {
        Name,
        Description,
        Value,
        State,
        Selection,
        Range,
        Scroll,
        Geometry,
        Text,
        Exposure,
        Metadata
    };

    /// @brief Units of an absolute or relative scroll payload.
    enum class ScrollUnit : std::uint8_t
    {
        Logical, ///< Application logical scroll coordinates/deltas.
        Percent, ///< Absolute [0, 100] percentages; -1 leaves an axis unchanged.
        Step     ///< Relative step commands: +/-1 small, +/-10 large, 0 unchanged.
    };
    /// @brief Distinguishes replacement, additive selection, and removal requests.
    enum class SelectionOperation : std::uint8_t
    {
        Replace,
        Add,
        Remove
    };
    /// @brief Scroll delta or destination, with explicit units and absolute/relative semantics.
    struct ScrollRequest
    {
        double horizontal = 0.0;               ///< Horizontal delta/destination in unit.
        double vertical = 0.0;                 ///< Vertical delta/destination in unit.
        bool absolute = false;                 ///< True for a destination, false for a relative delta.
        ScrollUnit unit = ScrollUnit::Logical; ///< Native backend payload units.
    };

    /// @brief Owning, typed action copied from a native accessibility client.
    struct ActionRequest
    {
        RequestId requestId = kInvalidRequestId;            ///< Bridge-assigned unique accepted transport identity.
        Generation observedGeneration = kInvalidGeneration; ///< Native client's queried generation; stale queued actions are delivered unchanged.
        NodeId node = kInvalidNodeId;                       ///< Target semantic identity, not a borrowed pointer.
        ActionKind action = ActionKind::Invoke;             ///< Requested application operation.
        double value = 0.0;                                 ///< Numeric SetValue payload in application range units.
        Point point;                                        ///< Logical point payload; text ScrollTo uses y=0 for top alignment, y=1 for bottom.
        ScrollRequest scroll;                               ///< Typed relative/absolute scrolling payload.
        std::optional<TextRange> range;                     ///< Optional UTF-8 range for selection or text ScrollTo.
        std::vector<TextRange> selection;                   ///< Owning optional text-selection ranges.
        std::string text;                                   ///< Owning strict UTF-8 string SetValue payload.
        SelectionOperation selectionOperation = SelectionOperation::Replace; ///< Select/SetTextSelection mutation mode.
    };

    /// @brief Portable description of one queued notification.
    struct Notification
    {
        NotificationKind kind = NotificationKind::TreeInvalidated; ///< Coarse diff/announcement event category.
        Generation generation = kInvalidGeneration;                ///< Committed generation responsible for the event.
        NodeId node = kInvalidNodeId;                              ///< Event source semantic identity.
        NodeId relatedNode = kInvalidNodeId;                       ///< Optional related semantic identity.
        PropertyKind property = PropertyKind::Name;                ///< Changed property category when applicable.
    };

    /// @brief Bounded queue telemetry for native action requests.
    struct ActionQueueInfo
    {
        std::uint32_t capacity = 0; ///< Fixed action slots for this activation.
        std::uint32_t pending = 0;  ///< Queued requests awaiting application consumption.
        std::uint64_t rejected = 0; ///< Queue-full rejection count; excludes argument/capability/lifecycle rejection.
    };

    /// @brief Bounded queue telemetry for native notifications.
    struct NotificationQueueInfo
    {
        std::uint32_t capacity = 0;       ///< Fixed notification slots for this activation.
        std::uint32_t pending = 0;        ///< Native notifications awaiting an owner-thread pump.
        std::uint64_t collapsed = 0;      ///< Pressure/marker refresh count; the committed snapshot is never rolled back.
        bool invalidationPending = false; ///< A pending tree invalidation supersedes intermediate diffs.
    };

    /// @brief Combined bridge queue telemetry.
    struct QueueInfo
    {
        ActionQueueInfo actions;             ///< Action queue telemetry, captured under its transport lock.
        NotificationQueueInfo notifications; ///< Notification queue telemetry, captured separately under its transport lock.
    };

    // ------------------------------------------------------------
    // Announcements
    // ------------------------------------------------------------

    /// @brief Priority of an explicit announcement.
    enum class AnnouncementPriority : std::uint8_t
    {
        Low,
        Normal,
        High
    };

    /// @brief Borrowed announcement submitted by application code.
    struct AnnouncementView
    {
        NodeId node = kInvalidNodeId; ///< Source semantic identity; zero selects the current root.
        std::string_view text;        ///< Borrowed strict UTF-8 copied before announce() returns.
        std::string_view language;    ///< Optional language hint; differing source-language overrides require AnnouncementLanguage support.
        AnnouncementPriority priority = AnnouncementPriority::Normal; ///< Native delivery urgency, not a guarantee of speech.
    };
} // namespace GameWIP::Desktop::Types::Accessibility

namespace GameWIP::Desktop::Accessibility
{
    // ------------------------------------------------------------
    // Validation
    // ------------------------------------------------------------

    /// @brief Validates all portable tree, text, geometry, relation, and limit invariants.
    /// @param snapshot Borrowed complete tree; all nested views must remain readable during the call.
    /// @param limits Positive capacities, all at most INT32_MAX; extension depth is at most 32.
    /// @return First semantic issue, or OutOfMemory/SizeLimitExceeded when validation resources fail.
    /// @note May allocate bounded temporary indexes; never calls a native backend or application callback.
    [[nodiscard]] DESKTOP_EXPORT Types::Accessibility::ValidationResult validate(
        const Types::Accessibility::SnapshotView &snapshot,
        const Types::Accessibility::Limits &limits = {}) noexcept;

    // ------------------------------------------------------------
    // Snapshot authoring
    // ------------------------------------------------------------

    /// @brief Owning snapshot authoring and deep-copy container.
    /// @details Externally synchronized. Moved-from builders remain reusable. Construction allocates nothing;
    /// checked mutations may allocate. addNode() enforces copy budgets and unique IDs; validate()/publish()
    /// enforces complete semantic correctness, permitting forward references while authoring.
    class DESKTOP_EXPORT SnapshotBuilder final
    {
    public:
        /// @name Lifecycle
        /// @{

        SnapshotBuilder() noexcept; ///< Creates an empty, allocation-free builder with default limits.
        explicit SnapshotBuilder(
            const Types::Accessibility::Limits &limits) noexcept; ///< Creates an empty builder with a copy of the supplied limits.
        ~SnapshotBuilder() noexcept;                              ///< Releases all owned snapshot data.

        SnapshotBuilder(const SnapshotBuilder &) = delete; ///< Owning builders cannot be copied; use copySnapshot() for an explicit deep copy.
        SnapshotBuilder &operator=(const SnapshotBuilder &) = delete;
        SnapshotBuilder(SnapshotBuilder &&) noexcept;            ///< Transfers owned authoring storage without allocating.
        SnapshotBuilder &operator=(SnapshotBuilder &&) noexcept; ///< Replaces authoring storage by moving without allocation.

        /// @}

        /// @name Authoring
        /// @{

        /// @brief Removes all owned nodes and resets generation and root.
        void clear() noexcept;

        /// @brief Returns the limits used when adding or copying data.
        [[nodiscard]] const Types::Accessibility::Limits &limits() const noexcept;

        /// @brief Sets the nonzero generation copied into the next view.
        [[nodiscard]] IO::Types::Status setGeneration(Types::Accessibility::Generation generation) noexcept;

        /// @brief Sets the application root node identity.
        [[nodiscard]] IO::Types::Status setRoot(Types::Accessibility::NodeId root) noexcept;

        /// @brief Deep-copies one borrowed node and all of its referenced data.
        /// @return InvalidArgument for invalid limits/zero or duplicate IDs; SizeLimitExceeded for copy-budget pressure;
        /// OutOfMemory for allocation failure. No node is appended on failure.
        [[nodiscard]] IO::Types::Status addNode(const Types::Accessibility::Node &node) noexcept;
        /// @}

        /// @name Inspection
        /// @{

        /// @brief Returns the number of owned nodes.
        [[nodiscard]] std::size_t nodeCount() const noexcept;

        /// @brief Returns a borrowed view into the builder's owned storage.
        /// @details The view remains valid until the builder is mutated or destroyed.
        [[nodiscard]] Types::Accessibility::SnapshotView view() const noexcept;
        /// @}

    private:
        std::unique_ptr<Detail::SnapshotBuilderState> state_;
        Types::Accessibility::Limits limits_;
    };

    // ------------------------------------------------------------
    // Immutable snapshot inspection
    // ------------------------------------------------------------

    /// @brief Read-only structural view of one published snapshot.
    /// @details Passive immutable retention. Independent reader copies may be inspected on any thread;
    /// replacing/destroying the same reader requires external synchronization. A reader survives publication,
    /// disable, Window close, and Window destruction. All returned views/pointers expire with the last retaining reader.
    class DESKTOP_EXPORT SnapshotReader final
    {
    public:
        /// @name Lifecycle
        /// @{

        SnapshotReader() noexcept = default; ///< Creates an empty, allocation-free reader.
        ~SnapshotReader() noexcept;          ///< Releases immutable retention, possibly reclaiming snapshot storage.

        SnapshotReader(const SnapshotReader &) noexcept = default;            ///< Shares immutable retention without allocating.
        SnapshotReader &operator=(const SnapshotReader &) noexcept = default; ///< Replaces immutable retention by sharing the source snapshot.
        SnapshotReader(SnapshotReader &&) noexcept = default;                 ///< Transfers immutable retention without allocating.
        SnapshotReader &operator=(SnapshotReader &&) noexcept = default;      ///< Replaces immutable retention by transferring the source snapshot.

        /// @}

        /// @name Inspection
        /// @{

        /// @brief Reports whether the reader retains a published snapshot.
        [[nodiscard]] bool isValid() const noexcept;

        /// @brief Returns metadata for the retained snapshot.
        [[nodiscard]] Types::Accessibility::SnapshotInfo info() const noexcept;

        /// @brief Returns a borrowed view retained by this reader.
        [[nodiscard]] Types::Accessibility::SnapshotView view() const noexcept;

        /// @brief Finds one node without allocation or native calls.
        /// @return Borrowed immutable node or nullptr; logarithmic in node count.
        [[nodiscard]] const Types::Accessibility::Node *find(Types::Accessibility::NodeId node) const noexcept;
        /// @}

    private:
        friend class Facade;
        explicit SnapshotReader(std::shared_ptr<const Detail::PublishedSnapshot> snapshot) noexcept;
        std::shared_ptr<const Detail::PublishedSnapshot> snapshot_;
    };

    // ------------------------------------------------------------
    // Window accessibility bridge
    // ------------------------------------------------------------

    /// @brief Window-integrated façade for the optional accessibility bridge.
    /// @details This is a lightweight non-owning view. Publication and inspection are safe from
    /// any thread after enable(); enable(), disable(), and action draining belong to the Window's
    /// owner thread. No application callback is ever invoked by the native provider.
    /// Initial enable() must complete before sharing the façade across threads. Join all façade users before
    /// destroying the borrowed Window. Concurrent publication may race close; the close gate prevents resurrection.
    /// There is no worker thread, UI toolkit dependency, rendering ownership, or semantic action execution.
    class DESKTOP_EXPORT Facade final
    {
    public:
        /// @name Activation
        /// @{

        explicit Facade(Window &window) noexcept; ///< Borrows Window without allocating; equivalent to Window::accessibility().

        /// @brief Explicitly enables the bridge for the current open Window lifetime.
        /// @return NotOpen, ResourceBusy on the wrong thread, AlreadyOpen, Unsupported, InvalidArgument for limits,
        /// or a resource failure. Failed enablement never exposes a native semantic tree.
        [[nodiscard]] IO::Types::Status enable(const Types::Accessibility::Options &options = {}) noexcept;

        /// @brief Disables the bridge and clears its active snapshot and queues.
        /// @return Success when already disabled; ResourceBusy for an enabled bridge called off its owner thread.
        /// @note Retained native providers become unavailable; retained portable readers remain usable.
        [[nodiscard]] IO::Types::Status disable() noexcept;

        /// @brief Reports whether the bridge is enabled for the current open lifetime.
        [[nodiscard]] bool enabled() const noexcept;

        /// @brief Returns native and portable feature support for the active bridge.
        [[nodiscard]] Types::Accessibility::Features features() const noexcept;

        /// @}

        /// @name Snapshot publication and inspection
        /// @{

        /// @brief Validates and copies a borrowed snapshot into immutable publication storage.
        /// @details Only a strictly newer nonzero generation may replace the active snapshot;
        /// generation order remains monotonic across this Window object's reopen cycles.
        /// Root identity is fixed until disable/close. Validation, ownership, UTF-16 indexes, and native preparation
        /// finish before replacement. A failed publication preserves the active tree. Notification pressure does
        /// not fail publication: it collapses diffs to a committed-generation invalidation marker.
        /// @return NotOpen for a closed gate; InvalidArgument for generation/root/semantic errors; SizeLimitExceeded
        /// for configured limits; OutOfMemory or a backend failure for preparation errors.
        [[nodiscard]] IO::Types::Status publish(const Types::Accessibility::SnapshotView &snapshot) noexcept;

        /// @brief Validates and copies an owning builder's current view.
        [[nodiscard]] IO::Types::Status publish(const SnapshotBuilder &snapshot) noexcept;

        /// @brief Returns metadata for the active snapshot without copying its tree.
        [[nodiscard]] Types::Accessibility::SnapshotInfo snapshotInfo() const noexcept;

        /// @brief Retains a read-only structural reader for allocation-free inspection.
        [[nodiscard]] SnapshotReader readSnapshot() const noexcept;

        /// @brief Deep-copies the active snapshot into caller-owned builder storage.
        /// @return NotOpen when no active snapshot; otherwise authoring-copy status. Destination is unchanged on failure.
        [[nodiscard]] IO::Types::Status copySnapshot(SnapshotBuilder &destination) const noexcept;

        /// @}

        /// @name Actions and notifications
        /// @{

        /// @brief Removes the oldest queued native action for owner-thread application handling.
        /// @return False without changing outAction when disabled, empty, or called on the wrong thread.
        /// @note Application code must revalidate node/generation and apply the action itself; acceptance is transport-only.
        [[nodiscard]] bool popAction(Types::Accessibility::ActionRequest &outAction) noexcept;

        /// @brief Returns bounded action and notification queue telemetry.
        /// @note Locks each queue separately; this is not a coherent joint snapshot of both queues.
        [[nodiscard]] Types::Accessibility::QueueInfo queueInfo() const noexcept;

        /// @brief Queues one bounded explicit announcement for native delivery.
        /// @return NotOpen, Unsupported (including unsupported language override), InvalidArgument, EncodingFailed,
        /// SizeLimitExceeded for full queues, or OutOfMemory. Delivery requires subsequent owner event pumping;
        /// acceptance does not guarantee that an assistive client speaks it. Native redaction is enforced at delivery.
        [[nodiscard]] IO::Types::Status announce(const Types::Accessibility::AnnouncementView &announcement) noexcept;
        /// @}

    private:
        Window *window_ = nullptr;
    };
} // namespace GameWIP::Desktop::Accessibility
