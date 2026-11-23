/// @file accessibility_publication.cpp
/// @brief Text indexing and semantic change notification helpers.

#include "desktop/internal/helpers/accessibility_publication.h"
#include "unicode/unicode.h"

#include <algorithm>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace GameWIP::Desktop::Internal::Helpers
{
    namespace A = Types::Accessibility;

    /// ------------------------------------------------------------
    /// UTF-16 conversion and text indexing
    /// ------------------------------------------------------------

    std::u16string toUtf16(std::string_view text)
    {
        std::u16string result(Unicode::Utf8::measureToUtf16(text).requiredCodeUnits, u'\0');
        static_cast<void>(Unicode::Utf8::convertToUtf16(text, result));
        return result;
    }

    /// @brief Preconverts native strings and indexes Unicode and annotation boundaries before publication.
    void indexAccessibilityText(const A::Node &node, AccessibilityTextCache &cache)
    {
        // Cache native strings before the snapshot becomes visible.
        cache.name = toUtf16(node.name);
        cache.description = toUtf16(node.description);
        cache.help = toUtf16(node.helpText);
        cache.value = toUtf16(node.value);
        cache.language = toUtf16(node.language);
        cache.accessKey = toUtf16(node.accessKey);
        cache.shortcut = toUtf16(node.keyboardShortcut);

        if(!node.text)
        {
            return;
        }
        const auto text = node.text->utf8;
        cache.text = toUtf16(text);
        cache.textLanguage = toUtf16(node.text->language);

        // Index annotation and grapheme boundaries for native text queries.
        for(const auto &annotation : node.text->annotations)
        {
            cache.annotationValues.push_back(toUtf16(annotation.value));
        }

        cache.formats = {0, static_cast<std::uint32_t>(text.size())};

        for(const auto &annotation : node.text->annotations)
        {
            cache.formats.push_back(annotation.range.begin);
            cache.formats.push_back(annotation.range.end);
        }

        std::ranges::sort(cache.formats);
        cache.formats.erase(std::unique(cache.formats.begin(), cache.formats.end()), cache.formats.end());

        std::vector<std::size_t> boundaries(text.size() + 1);
        Unicode::Utf8::GraphemeCursor cursor;
        const auto index = cursor.reset(text, boundaries);
        boundaries.resize(index.requiredBoundaryCount);

        for(auto offset : boundaries)
        {
            cache.graphemes.push_back(static_cast<std::uint32_t>(offset));
        }

        // Map UTF-8 scalars to UTF-16 offsets and derive word and line starts.
        std::uint32_t offset = 0, wideOffset = 0;
        cache.words.push_back(0);
        cache.lines.push_back(0);
        bool previousSpace = false;

        while(offset < text.size())
        {
            cache.scalars.push_back(offset);
            cache.utf16Offsets.push_back(wideOffset);
            const auto scalar = Unicode::Utf8::decodeScalar(text.substr(offset));
            const bool space = scalar.scalar == U' ' || scalar.scalar == U'\t' || scalar.scalar == U'\r' || scalar.scalar == U'\n' ||
                               scalar.scalar == 0xA0 || scalar.scalar == 0x1680 || (scalar.scalar >= 0x2000 && scalar.scalar <= 0x200A) ||
                               scalar.scalar == 0x2028 || scalar.scalar == 0x2029 || scalar.scalar == 0x202F || scalar.scalar == 0x205F ||
                               scalar.scalar == 0x3000;

            if(previousSpace && !space && std::ranges::binary_search(cache.graphemes, offset))
            {
                cache.words.push_back(offset);
            }
            previousSpace = space;
            offset += scalar.bytesConsumed;
            wideOffset += scalar.scalar > 0xFFFF ? 2U : 1U;

            if(scalar.scalar == U'\n' || scalar.scalar == 0x2028 || scalar.scalar == 0x2029)
            {
                cache.lines.push_back(offset);
            }
        }

        cache.scalars.push_back(offset);
        cache.utf16Offsets.push_back(wideOffset);

        if(cache.words.back() != offset)
        {
            cache.words.push_back(offset);
        }

        if(cache.lines.back() != offset)
        {
            cache.lines.push_back(offset);
        }
    }

    /// ------------------------------------------------------------
    /// Semantic change notifications
    /// ------------------------------------------------------------

    /// @brief Retains bounded committed-generation diffs, collapsing pressure into one current-tree invalidation.
    void queueAccessibilityDiff(
        Internal::AccessibilityRuntime &runtime,
        const std::shared_ptr<const Internal::PublishedSnapshot> &before,
        const std::shared_ptr<const Internal::PublishedSnapshot> &after)
    {
        std::lock_guard lock(runtime.notificationMutex);

        // Collapse stale property changes into one invalidation under queue pressure.
        const auto invalidation = [&]
        {
            for(auto &notification : runtime.notifications)
            {
                notification = {};
            }
            runtime.notificationHead = 0;
            runtime.notificationCount = 1;
            runtime.invalidationPending = true;
            ++runtime.collapsed;
            runtime.notifications[0] = {{A::NotificationKind::TreeInvalidated, after->storage.generation, after->storage.root}, before, after};
        };

        if(runtime.invalidationPending)
        {
            for(std::size_t i = 0; i < runtime.notificationCount; ++i)
            {
                auto &notification = runtime.notifications[(runtime.notificationHead + i) % runtime.notifications.size()];

                if(notification.notification.kind == A::NotificationKind::TreeInvalidated)
                {
                    notification.notification.generation = after->storage.generation;
                    notification.before.reset();
                    notification.after = after;
                    ++runtime.collapsed;
                    break;
                }
            }

            return;
        }

        const auto push = [&](A::NotificationKind kind, A::NodeId id, A::PropertyKind property = A::PropertyKind::Name)
        {
            if(runtime.invalidationPending)
            {
                return;
            }

            if(runtime.notificationCount == runtime.notifications.size())
            {
                invalidation();
                return;
            }
            runtime.notifications[(runtime.notificationHead + runtime.notificationCount++) % runtime.notifications.size()] = {
                {kind, after->storage.generation, id, 0, property},
                before,
                after};
        };

        if(!before)
        {
            push(A::NotificationKind::TreeInvalidated, after->storage.root);
            runtime.invalidationPending = true;
            return;
        }

        // Compare retained nodes and queue their property changes.
        bool structure = before->storage.nodes.size() != after->storage.nodes.size();
        const auto rectEqual = [](const A::Rect &a, const A::Rect &b)
        {
            return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
        };
        const auto optionalEqual = [](const auto &a, const auto &b, const auto &equal)
        {
            return a.has_value() == b.has_value() && (!a || equal(*a, *b));
        };
        const auto geometryEqual = [&](const A::Geometry &a, const A::Geometry &b)
        {
            const auto &x = a.toWindow;
            const auto &y = b.toWindow;
            return rectEqual(a.localBounds, b.localBounds) && optionalEqual(a.clippingBounds, b.clippingBounds, rectEqual) && x.m11 == y.m11 &&
                   x.m12 == y.m12 && x.m21 == y.m21 && x.m22 == y.m22 && x.tx == y.tx && x.ty == y.ty && a.visible == b.visible &&
                   a.hitTestable == b.hitTestable && a.hitTestOrder == b.hitTestOrder;
        };

        for(const auto &node : after->storage.nodes)
        {
            const auto *old = before->find(node.id);

            if(!old)
            {
                structure = true;
                continue;
            }

            // Detect tree changes before emitting node properties.
            if(node.parent != old->parent || !std::ranges::equal(node.children, old->children) || node.role != old->role)
            {
                structure = true;
            }

            // Queue common property, state, and selection changes.
            if(node.name != old->name)
            {
                push(A::NotificationKind::PropertyChanged, node.id, A::PropertyKind::Name);
            }

            if(node.description != old->description)
            {
                push(A::NotificationKind::PropertyChanged, node.id, A::PropertyKind::Description);
            }

            if(node.value != old->value || (node.rangeValue && (!old->rangeValue || node.rangeValue->value != old->rangeValue->value)))
            {
                push(A::NotificationKind::PropertyChanged, node.id, A::PropertyKind::Value);
            }

            if(!optionalEqual(
                   node.rangeValue,
                   old->rangeValue,
                   [](const auto &a, const auto &b)
                   {
                       return a.minimum == b.minimum && a.maximum == b.maximum && a.smallChange == b.smallChange && a.largeChange == b.largeChange;
                   }))
            {
                push(A::NotificationKind::PropertyChanged, node.id, A::PropertyKind::Range);
            }

            if(!optionalEqual(
                   node.scroll,
                   old->scroll,
                   [](const auto &a, const auto &b)
                   {
                       return a.horizontalScrollable == b.horizontalScrollable && a.verticalScrollable == b.verticalScrollable &&
                              a.horizontalPercent == b.horizontalPercent && a.verticalPercent == b.verticalPercent &&
                              a.horizontalViewSize == b.horizontalViewSize && a.verticalViewSize == b.verticalViewSize;
                   }))
            {
                push(A::NotificationKind::PropertyChanged, node.id, A::PropertyKind::Scroll);
            }

            if(node.states.flags != old->states.flags)
            {
                push(A::NotificationKind::PropertyChanged, node.id, A::PropertyKind::State);
            }

            if(node.states.contains(A::State::Focused) && !old->states.contains(A::State::Focused))
            {
                push(A::NotificationKind::FocusChanged, node.id);
            }

            if(node.states.contains(A::State::Selected) != old->states.contains(A::State::Selected) ||
               !optionalEqual(
                   node.selection,
                   old->selection,
                   [](const auto &a, const auto &b)
                   {
                       return a.activeNode == b.activeNode && std::ranges::equal(a.selectedNodes, b.selectedNodes);
                   }))
            {
                push(A::NotificationKind::SelectionChanged, node.id);
            }

            // Queue text, geometry, and exposure changes.
            if(node.text.has_value() != old->text.has_value() || (node.text && old->text && node.text->utf8 != old->text->utf8))
            {
                push(A::NotificationKind::TextChanged, node.id);
            }

            if(node.text && old->text &&
               (node.text->caret != old->text->caret || !std::ranges::equal(
                                                            node.text->selection,
                                                            old->text->selection,
                                                            [](auto left, auto right)
                                                            {
                                                                return left.begin == right.begin && left.end == right.end;
                                                            })))
            {
                push(A::NotificationKind::SelectionChanged, node.id, A::PropertyKind::Text);
            }

            if(!optionalEqual(node.geometry, old->geometry, geometryEqual))
            {
                push(A::NotificationKind::PropertyChanged, node.id, A::PropertyKind::Geometry);
            }

            if(node.exposure != old->exposure)
            {
                push(A::NotificationKind::PropertyChanged, node.id, A::PropertyKind::Exposure);
            }

            // Compare remaining metadata as one property change.
            const bool relationsEqual = std::ranges::equal(
                node.relations,
                old->relations,
                [](const auto &a, const auto &b)
                {
                    return a.kind == b.kind && std::ranges::equal(a.targets, b.targets);
                });
            const bool textMetadataEqual = optionalEqual(
                node.text,
                old->text,
                [&](const auto &a, const auto &b)
                {
                    return a.direction == b.direction && a.language == b.language && a.caret == b.caret &&
                           std::ranges::equal(
                               a.annotations,
                               b.annotations,
                               [](const auto &x, const auto &y)
                               {
                                   return x.range.begin == y.range.begin && x.range.end == y.range.end && x.kind == y.kind && x.value == y.value &&
                                          x.target == y.target;
                               }) &&
                           std::ranges::equal(
                               a.fragments,
                               b.fragments,
                               [&](const auto &x, const auto &y)
                               {
                                   return x.range.begin == y.range.begin && x.range.end == y.range.end && rectEqual(x.bounds, y.bounds);
                               });
                });
            const bool collectionEqual = optionalEqual(
                node.collection,
                old->collection,
                [](const auto &a, const auto &b)
                {
                    return a.rowCount == b.rowCount && a.columnCount == b.columnCount && a.rowIndex == b.rowIndex && a.columnIndex == b.columnIndex &&
                           a.rowSpan == b.rowSpan && a.columnSpan == b.columnSpan && a.positionInSet == b.positionInSet && a.setSize == b.setSize &&
                           a.level == b.level;
                });
            const bool virtualizationEqual = optionalEqual(
                node.virtualization,
                old->virtualization,
                [](const auto &a, const auto &b)
                {
                    return a.virtualized == b.virtualized && a.realized == b.realized && a.canRealize == b.canRealize &&
                           a.realizedChildCount == b.realizedChildCount && a.totalChildCount == b.totalChildCount;
                });

            if(node.actions.flags != old->actions.flags || node.helpText != old->helpText || node.language != old->language ||
               node.accessKey != old->accessKey || node.keyboardShortcut != old->keyboardShortcut || !relationsEqual || !textMetadataEqual ||
               !collectionEqual || !virtualizationEqual || node.selection.has_value() != old->selection.has_value() ||
               (node.selection && old->selection &&
                (node.selection->multiSelectable != old->selection->multiSelectable ||
                 node.selection->selectionRequired != old->selection->selectionRequired || node.selection->activeNode != old->selection->activeNode)))
            {
                push(A::NotificationKind::PropertyChanged, node.id, A::PropertyKind::Metadata);
            }
        }

        // Report structural changes after node-level changes.
        if(structure)
        {
            push(A::NotificationKind::StructureChanged, after->storage.root);
        }
    }

} // namespace GameWIP::Desktop::Internal::Helpers
