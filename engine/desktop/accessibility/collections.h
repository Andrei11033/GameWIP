/// @file collections.h
/// @brief Portable values, selection, and collection types.

#pragma once

#include "desktop/accessibility/identity.h"

#include <cstdint>
#include <span>

namespace GameWIP::Desktop::Types::Accessibility
{
    /// ------------------------------------------------------------
    /// Values, selection, and collections
    /// ------------------------------------------------------------

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
} // namespace GameWIP::Desktop::Types::Accessibility
