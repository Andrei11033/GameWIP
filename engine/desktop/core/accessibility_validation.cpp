/// @file accessibility_validation.cpp
/// @brief Complete portable validation before immutable semantic replacement.

#include "desktop/accessibility.h"
#include "unicode/unicode.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <new>

namespace GameWIP::Desktop::Accessibility
{
    namespace
    {
        namespace A = Types::Accessibility;
        using Issue = A::ValidationIssue;
        // ------------------------------------------------------------
        // Primitive validation and resource budgets
        // ------------------------------------------------------------

        bool validUtf8(std::string_view value) noexcept
        {
            return Unicode::Utf8::validate(value).outcome == Unicode::Types::ValidationOutcome::Valid;
        }

        bool validRect(const A::Rect &rect) noexcept
        {
            return std::isfinite(rect.x) && std::isfinite(rect.y) && std::isfinite(rect.width) && std::isfinite(rect.height) && rect.width >= 0 &&
                   rect.height >= 0 && std::isfinite(rect.x + rect.width) && std::isfinite(rect.y + rect.height);
        }
        struct Budget
        {
            const A::Limits &limits;
            std::uint64_t bytes = 0, nodeBytes = 0, extensionValues = 0;
            Issue string(std::string_view value)
            {
                if (value.size() > limits.maximumTextBytesPerNode || value.size() > limits.maximumTotalTextBytes ||
                    nodeBytes + value.size() > limits.maximumTextBytesPerNode || bytes + value.size() > limits.maximumTotalTextBytes)
                    return Issue::LimitExceeded;
                if (!validUtf8(value))
                    return Issue::InvalidText;
                nodeBytes += value.size();
                bytes += value.size();
                return Issue::None;
            }
            Issue extension(const A::ExtensionValue &value, std::uint32_t depth)
            {
                if (++extensionValues > limits.maximumExtensionValuesPerNode || depth > limits.maximumExtensionDepth)
                    return Issue::LimitExceeded;
                switch (value.kind)
                {
                case A::ExtensionValueKind::Boolean:
                case A::ExtensionValueKind::SignedInteger:
                case A::ExtensionValueKind::UnsignedInteger:
                    return Issue::None;
                case A::ExtensionValueKind::Double:
                    return std::isfinite(value.doubleValue) ? Issue::None : Issue::InvalidExtension;
                case A::ExtensionValueKind::Utf8:
                    return string(value.utf8Value);
                case A::ExtensionValueKind::NodeReference:
                    return value.nodeValue != 0 ? Issue::None : Issue::InvalidExtension;
                case A::ExtensionValueKind::List:
                    if (value.listValue.size() > limits.maximumExtensionListItems)
                        return Issue::LimitExceeded;
                    for (const auto &item : value.listValue)
                        if (const Issue issue = extension(item, depth + 1); issue != Issue::None)
                            return issue;
                    return Issue::None;
                }
                return Issue::InvalidExtension;
            }
        };

        A::ValidationResult fail(Issue issue, A::NodeId node = 0, std::size_t index = 0)
        {
            return {
                IO::makeStatus(issue == Issue::LimitExceeded ? IO::Types::ErrorCode::SizeLimitExceeded : IO::Types::ErrorCode::InvalidArgument),
                issue,
                node,
                index};
        }

        bool validLimits(const A::Limits &limits) noexcept
        {
            const std::array values{
                limits.maximumNodes,
                limits.maximumChildrenPerNode,
                limits.maximumRelationsPerNode,
                limits.maximumExtensionsPerNode,
                limits.maximumExtensionListItems,
                limits.maximumTextBytesPerNode,
                limits.maximumTotalTextBytes,
                limits.maximumTextAnnotationsPerNode,
                limits.maximumTextFragmentsPerNode,
                limits.maximumActionQueue,
                limits.maximumNotificationQueue,
                limits.maximumActionTextBytes,
                limits.maximumSelectionRangesPerAction,
                limits.maximumAnnouncementBytes,
                limits.maximumExtensionDepth,
                limits.maximumExtensionValuesPerNode,
                limits.maximumRelationTargetsPerNode,
                limits.maximumTextSelectionsPerNode,
                limits.maximumNativeProviders,
                limits.maximumNativeTextRanges};
            return std::ranges::all_of(
                       values,
                       [](auto value)
                       {
                           return value != 0 && value <= static_cast<std::uint32_t>(INT32_MAX);
                       }) &&
                   limits.maximumExtensionDepth <= 32 && limits.maximumNativeProviders >= limits.maximumNodes;
        }
    } // namespace

    // ------------------------------------------------------------
    // Complete snapshot validation
    // ------------------------------------------------------------

    A::ValidationResult validate(const A::SnapshotView &snapshot, const A::Limits &limits) noexcept
    {
        if (!validLimits(limits))
            return fail(Issue::InvalidLimits);
        if (snapshot.generation == 0)
            return fail(Issue::ZeroGeneration);
        if (snapshot.nodes.empty())
            return fail(Issue::EmptyTree);
        if (snapshot.nodes.size() > limits.maximumNodes)
            return fail(Issue::LimitExceeded);

        try
        {
            std::vector<std::pair<A::NodeId, std::size_t>> index;
            index.reserve(snapshot.nodes.size());
            for (std::size_t i = 0; i < snapshot.nodes.size(); ++i)
            {
                if (snapshot.nodes[i].id == 0)
                    return fail(Issue::InvalidNodeId, 0, i);
                index.emplace_back(snapshot.nodes[i].id, i);
            }
            std::ranges::sort(index);
            for (std::size_t i = 1; i < index.size(); ++i)
                if (index[i - 1].first == index[i].first)
                    return fail(Issue::DuplicateNodeId, index[i].first, index[i].second);

            const auto find = [&](A::NodeId id)
            {
                const auto found = std::lower_bound(
                    index.begin(),
                    index.end(),
                    id,
                    [](const auto &entry, auto key)
                    {
                        return entry.first < key;
                    });
                return found != index.end() && found->first == id ? found->second : snapshot.nodes.size();
            };
            const std::size_t root = find(snapshot.root);
            if (root == snapshot.nodes.size() || snapshot.nodes[root].parent != 0)
                return fail(Issue::InvalidRoot, snapshot.root);

            Budget budget{limits};
            std::vector<std::uint32_t> incoming(snapshot.nodes.size());
            std::size_t focused = 0;
            for (std::size_t i = 0; i < snapshot.nodes.size(); ++i)
            {
                const auto &node = snapshot.nodes[i];
                budget.nodeBytes = 0;
                budget.extensionValues = 0;
                if (node.role >= A::Role::Count)
                    return fail(Issue::InvalidRole, node.id, i);
                if ((node.states.flags >> 23U) != 0 || (node.states.contains(A::State::Focused) && !node.states.contains(A::State::Focusable)) ||
                    (node.states.contains(A::State::Mixed) && node.states.contains(A::State::Checked)))
                    return fail(Issue::InvalidState, node.id, i);
                if (node.states.contains(A::State::Focused) && ++focused > 1)
                    return fail(Issue::InvalidState, node.id, i);
                if ((node.actions.flags >> static_cast<unsigned>(A::ActionKind::Count)) != 0)
                    return fail(Issue::InvalidAction, node.id, i);
                if (node.exposure > A::Exposure::RedactContent)
                    return fail(Issue::InvalidValue, node.id, i);
                for (auto text : {node.name, node.description, node.helpText, node.value, node.language, node.accessKey, node.keyboardShortcut})
                    if (const Issue issue = budget.string(text); issue != Issue::None)
                        return fail(issue, node.id, i);
                if (node.language.find('\0') != std::string_view::npos)
                    return fail(Issue::InvalidText, node.id, i);
                if (node.children.size() > limits.maximumChildrenPerNode || node.relations.size() > limits.maximumRelationsPerNode ||
                    node.extensions.size() > limits.maximumExtensionsPerNode)
                    return fail(Issue::LimitExceeded, node.id, i);
                std::vector<A::NodeId> children(node.children.begin(), node.children.end());
                std::ranges::sort(children);
                if (std::adjacent_find(children.begin(), children.end()) != children.end())
                    return fail(Issue::DuplicateChild, node.id, i);
                for (auto child : node.children)
                {
                    const auto childIndex = find(child);
                    if (childIndex == snapshot.nodes.size())
                        return fail(Issue::MissingNode, node.id, i);
                    if (snapshot.nodes[childIndex].parent != node.id || ++incoming[childIndex] > 1)
                        return fail(Issue::ParentMismatch, child, i);
                }
                if (i != root && (node.parent == 0 || find(node.parent) == snapshot.nodes.size()))
                    return fail(Issue::ParentMismatch, node.id, i);
                std::uint64_t relationTargets = 0;
                std::uint64_t relationKinds = 0;
                for (const auto &relation : node.relations)
                {
                    if (relation.kind > A::RelationKind::ColumnHeaders || relation.targets.empty())
                        return fail(Issue::InvalidRelation, node.id, i);
                    const auto kind = std::uint64_t{1} << static_cast<unsigned>(relation.kind);
                    if ((relationKinds & kind) != 0)
                        return fail(Issue::InvalidRelation, node.id, i);
                    relationKinds |= kind;
                    relationTargets += relation.targets.size();
                    if (relationTargets > limits.maximumRelationTargetsPerNode)
                        return fail(Issue::LimitExceeded, node.id, i);
                    std::vector<A::NodeId> targets(relation.targets.begin(), relation.targets.end());
                    std::ranges::sort(targets);
                    if (std::adjacent_find(targets.begin(), targets.end()) != targets.end())
                        return fail(Issue::InvalidRelation, node.id, i);
                    for (auto target : relation.targets)
                        if (find(target) == snapshot.nodes.size())
                            return fail(Issue::MissingNode, node.id, i);
                }
                const auto checkReferences = [&](const auto &self, const A::ExtensionValue &value) -> bool
                {
                    if (value.kind == A::ExtensionValueKind::NodeReference)
                        return find(value.nodeValue) != snapshot.nodes.size();
                    if (value.kind == A::ExtensionValueKind::List)
                        for (const auto &item : value.listValue)
                            if (!self(self, item))
                                return false;
                    return true;
                };
                for (std::size_t e = 0; e < node.extensions.size(); ++e)
                {
                    const auto &extension = node.extensions[e];
                    if (extension.namespaceName.empty() || extension.name.empty())
                        return fail(Issue::InvalidExtension, node.id, e);
                    for (std::size_t previous = 0; previous < e; ++previous)
                        if (node.extensions[previous].namespaceName == extension.namespaceName && node.extensions[previous].name == extension.name)
                            return fail(Issue::InvalidExtension, node.id, e);
                    for (auto text : {extension.namespaceName, extension.name})
                        if (const Issue issue = budget.string(text); issue != Issue::None)
                            return fail(issue, node.id, e);
                    if (const Issue issue = budget.extension(extension.value, 0); issue != Issue::None)
                        return fail(issue, node.id, e);
                    if (!checkReferences(checkReferences, extension.value))
                        return fail(Issue::MissingNode, node.id, e);
                }

                if (node.geometry)
                {
                    const auto &g = *node.geometry;
                    const auto &t = g.toWindow;
                    if (!validRect(g.localBounds) || (g.clippingBounds && !validRect(*g.clippingBounds)) || !std::isfinite(t.m11) ||
                        !std::isfinite(t.m12) || !std::isfinite(t.m21) || !std::isfinite(t.m22) || !std::isfinite(t.tx) || !std::isfinite(t.ty))
                        return fail(Issue::InvalidGeometry, node.id, i);
                    const double determinant = t.m11 * t.m22 - t.m12 * t.m21;
                    if (!std::isfinite(determinant) || (g.hitTestable && determinant == 0))
                        return fail(Issue::InvalidGeometry, node.id, i);
                    double left = std::numeric_limits<double>::max(), top = left, right = -left, bottom = -left;
                    for (double x : {g.localBounds.x, g.localBounds.x + g.localBounds.width})
                        for (double y : {g.localBounds.y, g.localBounds.y + g.localBounds.height})
                        {
                            const double projectedX = t.m11 * x + t.m21 * y + t.tx, projectedY = t.m12 * x + t.m22 * y + t.ty;
                            if (!std::isfinite(projectedX) || !std::isfinite(projectedY))
                                return fail(Issue::InvalidGeometry, node.id, i);
                            left = std::min(left, projectedX);
                            right = std::max(right, projectedX);
                            top = std::min(top, projectedY);
                            bottom = std::max(bottom, projectedY);
                        }
                    if (!std::isfinite(right - left) || !std::isfinite(bottom - top))
                        return fail(Issue::InvalidGeometry, node.id, i);
                }

                if (node.rangeValue)
                {
                    const auto &r = *node.rangeValue;
                    if (!std::isfinite(r.value) || !std::isfinite(r.minimum) || !std::isfinite(r.maximum) || !std::isfinite(r.smallChange) ||
                        !std::isfinite(r.largeChange) || r.minimum > r.value || r.value > r.maximum || r.smallChange < 0 || r.largeChange < 0)
                        return fail(Issue::InvalidValue, node.id, i);
                }

                if (node.scroll)
                {
                    const auto &s = *node.scroll;
                    for (double value : {s.horizontalPercent, s.verticalPercent, s.horizontalViewSize, s.verticalViewSize})
                        if (!std::isfinite(value) || value < 0 || value > 100)
                            return fail(Issue::InvalidValue, node.id, i);
                }

                if (node.selection)
                {
                    const auto &s = *node.selection;
                    if ((!s.multiSelectable && s.selectedNodes.size() > 1) || (s.selectionRequired && s.selectedNodes.empty()))
                        return fail(Issue::InvalidSelection, node.id, i);
                    if (s.selectedNodes.size() > limits.maximumNodes)
                        return fail(Issue::LimitExceeded, node.id, i);
                    std::vector<A::NodeId> selected(s.selectedNodes.begin(), s.selectedNodes.end());
                    std::ranges::sort(selected);
                    if (std::adjacent_find(selected.begin(), selected.end()) != selected.end())
                        return fail(Issue::InvalidSelection, node.id, i);
                    if (s.activeNode != 0 && !std::ranges::binary_search(selected, s.activeNode))
                        return fail(Issue::InvalidSelection, node.id, i);
                    for (auto id : selected)
                        if (find(id) == snapshot.nodes.size() || !snapshot.nodes[find(id)].states.contains(A::State::Selected) ||
                            !snapshot.nodes[find(id)].states.contains(A::State::Selectable))
                            return fail(Issue::InvalidSelection, node.id, i);
                }

                if (node.collection)
                {
                    const auto &c = *node.collection;
                    for (const auto value :
                         {c.rowCount, c.columnCount, c.rowIndex, c.columnIndex, c.rowSpan, c.columnSpan, c.positionInSet, c.setSize, c.level})
                        if (value > static_cast<std::uint32_t>(INT32_MAX))
                            return fail(Issue::InvalidCollection, node.id, i);
                    if (c.rowSpan == 0 || c.columnSpan == 0 ||
                        (c.rowCount != 0 && (c.rowIndex >= c.rowCount || c.rowSpan > c.rowCount - c.rowIndex)) ||
                        (c.columnCount != 0 && (c.columnIndex >= c.columnCount || c.columnSpan > c.columnCount - c.columnIndex)) ||
                        (c.setSize != 0 && c.positionInSet > c.setSize) || c.rowCount > static_cast<std::uint32_t>(INT32_MAX) ||
                        c.columnCount > static_cast<std::uint32_t>(INT32_MAX))
                        return fail(Issue::InvalidCollection, node.id, i);
                }

                if (node.virtualization)
                {
                    const auto &v = *node.virtualization;
                    if (v.realizedChildCount > v.totalChildCount || v.realizedChildCount > node.children.size() || (!v.realized && !v.virtualized) ||
                        (v.canRealize && !node.actions.contains(A::ActionKind::Realize)))
                        return fail(Issue::InvalidVirtualization, node.id, i);
                }

                if (node.text)
                {
                    const auto &text = *node.text;
                    if (const Issue issue = budget.string(text.utf8); issue != Issue::None)
                        return fail(issue, node.id, i);
                    if (const Issue issue = budget.string(text.language); issue != Issue::None)
                        return fail(issue, node.id, i);
                    if (text.language.find('\0') != std::string_view::npos)
                        return fail(Issue::InvalidText, node.id, i);
                    if (text.direction > A::TextDirection::Mixed)
                        return fail(Issue::InvalidText, node.id, i);
                    if (text.annotations.size() > limits.maximumTextAnnotationsPerNode ||
                        text.fragments.size() > limits.maximumTextFragmentsPerNode || text.selection.size() > limits.maximumTextSelectionsPerNode)
                        return fail(Issue::LimitExceeded, node.id, i);
                    if (text.selection.size() > 1 && (!node.selection || !node.selection->multiSelectable))
                        return fail(Issue::InvalidSelection, node.id, i);
                    std::vector<std::size_t> boundaries(text.utf8.size() + 1);
                    Unicode::Utf8::GraphemeCursor cursor;
                    const auto segmented = cursor.reset(text.utf8, boundaries);
                    boundaries.resize(segmented.requiredBoundaryCount);
                    const auto validRange = [&](A::TextRange range)
                    {
                        return range.begin <= range.end && range.end <= text.utf8.size() && std::ranges::binary_search(boundaries, range.begin) &&
                               std::ranges::binary_search(boundaries, range.end);
                    };
                    for (const auto &annotation : text.annotations)
                    {
                        if (!validRange(annotation.range))
                            return fail(Issue::InvalidTextRange, node.id, i);
                        if (annotation.kind > A::TextAnnotationKind::Custom)
                            return fail(Issue::InvalidText, node.id, i);
                        if (annotation.kind == A::TextAnnotationKind::Language && annotation.value.find('\0') != std::string_view::npos)
                            return fail(Issue::InvalidText, node.id, i);
                        if (annotation.target != 0 && find(annotation.target) == snapshot.nodes.size())
                            return fail(Issue::MissingNode, node.id, i);
                        if (const Issue issue = budget.string(annotation.value); issue != Issue::None)
                            return fail(issue, node.id, i);
                    }
                    for (const auto &fragment : text.fragments)
                        if (!validRange(fragment.range) || !validRect(fragment.bounds))
                            return fail(Issue::InvalidTextRange, node.id, i);
                    for (auto range : text.selection)
                        if (!validRange(range))
                            return fail(Issue::InvalidTextRange, node.id, i);
                    if (text.caret && !validRange({*text.caret, *text.caret}))
                        return fail(Issue::InvalidTextRange, node.id, i);
                }
            }
            if (incoming[root] != 0)
                return fail(Issue::Cycle, snapshot.root);
            for (std::size_t i = 0; i < incoming.size(); ++i)
                if (i != root && incoming[i] != 1)
                    return fail(Issue::ParentMismatch, snapshot.nodes[i].id, i);
            std::vector<std::size_t> work{root};
            std::vector<bool> visited(snapshot.nodes.size());
            for (std::size_t cursor = 0; cursor < work.size(); ++cursor)
            {
                const auto i = work[cursor];
                if (visited[i])
                    return fail(Issue::Cycle, snapshot.nodes[i].id, i);
                visited[i] = true;
                for (auto child : snapshot.nodes[i].children)
                    work.push_back(find(child));
            }
            if (work.size() != snapshot.nodes.size())
                return fail(Issue::UnreachableNode);

            // Preorder intervals validate containment without a quadratic walk up every parent chain.
            std::vector<std::size_t> pending{root}, traversal, ordinal(snapshot.nodes.size()), subtree(snapshot.nodes.size(), 1),
                grid(snapshot.nodes.size(), snapshot.nodes.size());
            traversal.reserve(snapshot.nodes.size());
            pending.reserve(snapshot.nodes.size());
            while (!pending.empty())
            {
                const auto i = pending.back();
                pending.pop_back();
                ordinal[i] = traversal.size();
                traversal.push_back(i);
                const auto &node = snapshot.nodes[i];
                if (node.parent != 0)
                {
                    const auto parent = find(node.parent);
                    grid[i] = snapshot.nodes[parent].role == A::Role::Table && snapshot.nodes[parent].collection ? parent : grid[parent];
                }
                for (auto child = node.children.rbegin(); child != node.children.rend(); ++child)
                    pending.push_back(find(*child));
            }
            for (auto i = traversal.rbegin(); i != traversal.rend(); ++i)
                if (snapshot.nodes[*i].parent != 0)
                    subtree[find(snapshot.nodes[*i].parent)] += subtree[*i];
            for (std::size_t i = 0; i < snapshot.nodes.size(); ++i)
            {
                const auto &node = snapshot.nodes[i];

                if (node.selection)
                    for (auto selected : node.selection->selectedNodes)
                    {
                        const auto position = ordinal[find(selected)];
                        if (position <= ordinal[i] || position >= ordinal[i] + subtree[i])
                            return fail(Issue::InvalidSelection, node.id, i);
                    }
                if (node.collection && grid[i] < snapshot.nodes.size() &&
                    (node.role == A::Role::Cell || node.role == A::Role::RowHeader || node.role == A::Role::ColumnHeader))
                {
                    const auto &item = *node.collection;
                    const auto &container = *snapshot.nodes[grid[i]].collection;
                    if (item.rowIndex >= container.rowCount || item.rowSpan > container.rowCount - item.rowIndex ||
                        item.columnIndex >= container.columnCount || item.columnSpan > container.columnCount - item.columnIndex)
                        return fail(Issue::InvalidCollection, node.id, i);
                }
            }
            return {};
        }
        catch (const std::bad_alloc &)
        {
            return {IO::makeStatus(IO::Types::ErrorCode::OutOfMemory)};
        }
        catch (...)
        {
            return {IO::makeStatus(IO::Types::ErrorCode::SizeLimitExceeded), Issue::LimitExceeded};
        }
    }
} // namespace GameWIP::Desktop::Accessibility
