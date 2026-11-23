/// @file identity.h
/// @brief Portable identity and state types.

#pragma once

#include <cstdint>

namespace GameWIP::Desktop::Types::Accessibility
{
    /// ------------------------------------------------------------
    /// Identity and semantic state
    /// ------------------------------------------------------------

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
        Disabled = std::uint64_t{1} << 0U,
        Focusable = std::uint64_t{1} << 1U,
        Focused = std::uint64_t{1} << 2U,
        Offscreen = std::uint64_t{1} << 3U,
        Invisible = std::uint64_t{1} << 4U,
        ReadOnly = std::uint64_t{1} << 5U,
        Required = std::uint64_t{1} << 6U,
        Selected = std::uint64_t{1} << 7U,
        Selectable = std::uint64_t{1} << 8U,
        Expanded = std::uint64_t{1} << 9U,
        Expandable = std::uint64_t{1} << 10U,
        Checked = std::uint64_t{1} << 11U,
        Mixed = std::uint64_t{1} << 12U,
        Pressed = std::uint64_t{1} << 13U,
        Password = std::uint64_t{1} << 14U,
        Multiline = std::uint64_t{1} << 15U,
        HasPopup = std::uint64_t{1} << 16U,
        Modal = std::uint64_t{1} << 17U,
        Busy = std::uint64_t{1} << 18U,
        Visited = std::uint64_t{1} << 19U,
        Invalid = std::uint64_t{1} << 20U,
        Editable = std::uint64_t{1} << 21U,
        Sensitive = std::uint64_t{1} << 22U
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

    /// ------------------------------------------------------------
    /// Capabilities and exposure
    /// ------------------------------------------------------------

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
} // namespace GameWIP::Desktop::Types::Accessibility
