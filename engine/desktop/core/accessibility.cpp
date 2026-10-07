/// @file accessibility.cpp
/// @brief Thread-safe semantic publication and bounded accessibility transport.

#include "desktop/internal/accessibility_state.h"
#include "desktop/internal/window_platform.h"
#include "desktop/internal/desktop_test_hooks.h"
#include "unicode/unicode.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <new>

namespace GameWIP::Desktop::Detail
{
    // ------------------------------------------------------------
    // State ownership and host publication
    // ------------------------------------------------------------

    void AccessibilityStateDeleter::operator()(AccessibilityState *state) const noexcept
    {
        delete state;
    }

    void AccessibilityRuntime::publishGeometry(const AccessibilityHostGeometry &geometry) noexcept
    {
        geometrySequence.fetch_add(1, std::memory_order_seq_cst);
        x.store(geometry.x);
        y.store(geometry.y);
        width.store(geometry.width);
        height.store(geometry.height);
        scaleX.store(geometry.scaleX);
        scaleY.store(geometry.scaleY);
        visible.store(geometry.visible);
        focused.store(geometry.focused);
        geometrySequence.fetch_add(1, std::memory_order_seq_cst);
    }

    bool AccessibilityRuntime::readGeometry(AccessibilityHostGeometry &geometry) const noexcept
    {
        for (unsigned attempt = 0; attempt < 8; ++attempt)
        {
            const auto sequence = geometrySequence.load();
            if ((sequence & 1U) != 0)
            {
                continue;
            }
            geometry = {x.load(), y.load(), width.load(), height.load(), scaleX.load(), scaleY.load(), visible.load(), focused.load()};
            if (sequence == geometrySequence.load())
            {
                return true;
            }
        }

        return false;
    }

    void publishAccessibilityGeometry(WindowState &window) noexcept
    {
        if (!window.accessibility)
        {
            return;
        }
        const auto runtime = window.accessibility->runtime.load();
        if (!runtime || !runtime->live.load())
        {
            return;
        }
        AccessibilityHostGeometry before;
        const AccessibilityHostGeometry after{
            static_cast<double>(window.clientPosition.x),
            static_cast<double>(window.clientPosition.y),
            static_cast<double>(window.clientSize.width),
            static_cast<double>(window.clientSize.height),
            static_cast<double>(window.contentScale.x),
            static_cast<double>(window.contentScale.y),
            window.visible && window.presentation != Types::PresentationState::Minimized,
            window.focused};
        const bool changed = !runtime->readGeometry(before) || before.x != after.x || before.y != after.y || before.width != after.width ||
                             before.height != after.height || before.scaleX != after.scaleX || before.scaleY != after.scaleY ||
                             before.visible != after.visible || before.focused != after.focused;
        runtime->publishGeometry(after);
        if (changed)
        {
            invalidateAccessibilityHost(window, !before.focused && after.focused);
        }
    }

    void invalidateAccessibilityHost(WindowState &window, bool focusGained) noexcept
    {
        if (!window.accessibility)
        {
            return;
        }
        const auto runtime = window.accessibility->runtime.load();
        if (!runtime || !runtime->live.load())
        {
            return;
        }
        std::lock_guard lock(runtime->notificationMutex);
        const auto snapshot = runtime->snapshot.load();
        if (!snapshot || !runtime->live.load())
        {
            return;
        }
        if (runtime->invalidationPending)
        {
            for (std::size_t i = 0; i < runtime->notificationCount; ++i)
            {
                auto &notification = runtime->notifications[(runtime->notificationHead + i) % runtime->notifications.size()];
                if (notification.notification.kind == A::NotificationKind::TreeInvalidated)
                {
                    notification.notification.generation = snapshot->storage.generation;
                    notification.after = snapshot;
                    break;
                }
            }
        }
        else
        {
            if (runtime->notificationCount == runtime->notifications.size())
            {
                for (auto &notification : runtime->notifications)
                {
                    notification = {};
                }
                runtime->notificationHead = 0;
                runtime->notificationCount = 0;
                ++runtime->collapsed;
            }
            runtime->invalidationPending = true;
            runtime->notifications[(runtime->notificationHead + runtime->notificationCount++) % runtime->notifications.size()] = {
                {A::NotificationKind::TreeInvalidated, snapshot->storage.generation, snapshot->storage.root},
                snapshot,
                snapshot};
        }
        if (focusGained && snapshot->focusedNode != 0)
        {
            if (runtime->notificationCount < runtime->notifications.size())
            {
                runtime->notifications[(runtime->notificationHead + runtime->notificationCount++) % runtime->notifications.size()] = {
                    {A::NotificationKind::FocusChanged, snapshot->storage.generation, snapshot->focusedNode},
                    snapshot,
                    snapshot};
            }
            else
            {
                ++runtime->collapsed;
            }
        }
        Platform::wakeAccessibility(*runtime);
    }

    void closeAccessibility(WindowState &window) noexcept
    {
        if (!window.accessibility)
        {
            return;
        }
        auto &state = *window.accessibility;
        const auto runtime = state.runtime.load();
        if (!runtime || !runtime->live.exchange(false))
        {
            return;
        }
        std::lock_guard publication(state.publicationMutex);
        runtime->snapshot.store({});
        {
            std::lock_guard actions(runtime->actionMutex);
            for (auto &action : runtime->actions)
            {
                action = {};
            }
            runtime->actionCount = 0;
            runtime->actionHead = 0;
        }
        {
            std::lock_guard notifications(runtime->notificationMutex);
            for (auto &notification : runtime->notifications)
            {
                notification = {};
            }
            runtime->notificationCount = 0;
            runtime->notificationHead = 0;
            runtime->invalidationPending = false;
        }
        Platform::detachAccessibility(*runtime);
        runtime->nativeWindow.store(0);
    }

    // ------------------------------------------------------------
    // Bounded action and notification transport
    // ------------------------------------------------------------

    A::ActionResult submitAccessibilityAction(AccessibilityRuntime &runtime, A::ActionRequest request) noexcept
    {
        if (!runtime.live.load())
        {
            return {A::ActionAcceptance::Closed};
        }
        const auto snapshot = runtime.snapshot.load();
        const auto *node = snapshot ? snapshot->find(request.node) : nullptr;
        if (!node)
        {
            return {A::ActionAcceptance::Invalid};
        }
        if (request.action >= A::ActionKind::Count || !node->actions.contains(request.action))
        {
            return {A::ActionAcceptance::Unsupported};
        }
        if (node->states.contains(A::State::Disabled) || !runtime.hostEnabled.load())
        {
            return {A::ActionAcceptance::Rejected};
        }
        if (!std::isfinite(request.value) || !std::isfinite(request.point.x) || !std::isfinite(request.point.y) ||
            !std::isfinite(request.scroll.horizontal) || !std::isfinite(request.scroll.vertical) ||
            request.text.size() > runtime.limits.maximumActionTextBytes ||
            request.selection.size() > runtime.limits.maximumSelectionRangesPerAction ||
            Unicode::Utf8::validate(request.text).outcome != Unicode::Types::ValidationOutcome::Valid)
        {
            return {A::ActionAcceptance::Invalid};
        }
        if (request.action == A::ActionKind::SetValue && node->states.contains(A::State::ReadOnly))
        {
            return {A::ActionAcceptance::Rejected};
        }
        if (request.action == A::ActionKind::SetValue && node->rangeValue &&
            (request.value < node->rangeValue->minimum || request.value > node->rangeValue->maximum))
        {
            return {A::ActionAcceptance::Invalid};
        }
        if (request.scroll.unit > A::ScrollUnit::Step || request.selectionOperation > A::SelectionOperation::Remove)
        {
            return {A::ActionAcceptance::Invalid};
        }
        if (request.action == A::ActionKind::SetTextSelection || request.range || !request.selection.empty())
        {
            if (!node->text)
            {
                return {A::ActionAcceptance::Unsupported};
            }
            const auto &boundaries = snapshot->textCache[snapshot->findIndex(node->id)].graphemes;
            const auto valid = [&](A::TextRange range)
            {
                return range.begin <= range.end && std::ranges::binary_search(boundaries, range.begin) &&
                       std::ranges::binary_search(boundaries, range.end);
            };
            if ((request.range && !valid(*request.range)) || std::ranges::any_of(
                                                                 request.selection,
                                                                 [&](auto range)
                                                                 {
                                                                     return !valid(range);
                                                                 }))
            {
                return {A::ActionAcceptance::Invalid};
            }
        }
        std::lock_guard lock(runtime.actionMutex);
        if (!runtime.live.load())
        {
            return {A::ActionAcceptance::Closed};
        }
        if (runtime.nextRequest == 0)
        {
            return {A::ActionAcceptance::Rejected};
        }
        if (request.observedGeneration == 0)
        {
            request.observedGeneration = snapshot->storage.generation;
        }
        if (request.observedGeneration > snapshot->storage.generation)
        {
            return {A::ActionAcceptance::Invalid};
        }
        request.requestId = runtime.nextRequest++;
        const A::ActionResult result{A::ActionAcceptance::Accepted, request.requestId, request.observedGeneration};
        // Replace only the tail: crossing a durable request would reorder its observable effects.
        const bool coalescible = request.action == A::ActionKind::Focus || request.action == A::ActionKind::SetValue ||
                                 request.action == A::ActionKind::ScrollTo || request.action == A::ActionKind::Realize;
        if (coalescible && runtime.actionCount != 0)
        {
            auto &tail = runtime.actions[(runtime.actionHead + runtime.actionCount - 1) % runtime.actions.size()];
            if (tail.node == request.node && tail.action == request.action)
            {
                tail = std::move(request);
                Platform::wakeAccessibility(runtime);
                return result;
            }
        }
        if (runtime.actionCount == runtime.actions.size())
        {
            ++runtime.rejected;
            return {A::ActionAcceptance::QueueFull, 0, request.observedGeneration};
        }
        runtime.actions[(runtime.actionHead + runtime.actionCount++) % runtime.actions.size()] = std::move(request);
        Platform::wakeAccessibility(runtime);
        return result;
    }

    void drainAccessibilityNotifications(WindowState &window) noexcept
    {
        if (!window.accessibility)
        {
            return;
        }
        const auto runtime = window.accessibility->runtime.load();
        if (!runtime || runtime->ownerThread != std::this_thread::get_id())
        {
            return;
        }
        // A bounded batch prevents reentrant publications from extending a pump indefinitely.
        const std::size_t capacity = runtime->notifications.size();
        for (std::size_t i = 0; i < capacity; ++i)
        {
            AccessibilityPendingNotification notification;
            {
                std::lock_guard lock(runtime->notificationMutex);
                if (!runtime->live.load() || runtime->notificationCount == 0)
                {
                    return;
                }
                auto &slot = runtime->notifications[runtime->notificationHead];
                notification = std::move(slot);
                slot = {};
                runtime->notificationHead = (runtime->notificationHead + 1) % capacity;
                --runtime->notificationCount;
                if (notification.notification.kind == A::NotificationKind::TreeInvalidated)
                {
                    runtime->invalidationPending = false;
                }
            }
            Platform::deliverAccessibilityNotification(*runtime, notification);
        }
    }
} // namespace GameWIP::Desktop::Detail

namespace GameWIP::Desktop
{
    Accessibility::Facade Window::accessibility() noexcept
    {
        return Accessibility::Facade(*this);
    }
} // namespace GameWIP::Desktop

#if DESKTOP_INTERNAL_TEST_HOOKS
namespace GameWIP::Desktop::TestHooks
{
    Types::Accessibility::ActionResult submitAccessibilityAction(Window &window, Types::Accessibility::ActionRequest request) noexcept
    {
        const auto *state = Detail::WindowAccess::accessibilityState(window);
        const auto runtime = state ? state->runtime.load() : nullptr;
        return runtime ? Detail::submitAccessibilityAction(*runtime, std::move(request))
                       : Types::Accessibility::ActionResult{Types::Accessibility::ActionAcceptance::Closed};
    }

    bool popAccessibilityNotification(Window &window, Types::Accessibility::Notification &out) noexcept
    {
        const auto *state = Detail::WindowAccess::accessibilityState(window);
        const auto runtime = state ? state->runtime.load() : nullptr;
        if (!runtime || runtime->ownerThread != std::this_thread::get_id())
        {
            return false;
        }
        std::lock_guard lock(runtime->notificationMutex);
        if (!runtime->live.load() || runtime->notificationCount == 0)
        {
            return false;
        }
        auto &slot = runtime->notifications[runtime->notificationHead];
        out = slot.notification;
        slot = {};
        runtime->notificationHead = (runtime->notificationHead + 1) % runtime->notifications.size();
        --runtime->notificationCount;
        if (out.kind == Types::Accessibility::NotificationKind::TreeInvalidated)
        {
            runtime->invalidationPending = false;
        }
        return true;
    }
} // namespace GameWIP::Desktop::TestHooks
#endif

namespace GameWIP::Desktop::Accessibility
{
    namespace
    {
        namespace A = Types::Accessibility;
        // ------------------------------------------------------------
        // Snapshot preparation
        // ------------------------------------------------------------

        std::shared_ptr<Detail::AccessibilityRuntime> runtimeFor(const Window *window) noexcept
        {
            auto *state = window ? Detail::WindowAccess::accessibilityState(*window) : nullptr;
            return state ? state->runtime.load() : nullptr;
        }

        std::u16string utf16(std::string_view text)
        {
            std::u16string result(Unicode::Utf8::measureToUtf16(text).requiredCodeUnits, u'\0');
            static_cast<void>(Unicode::Utf8::convertToUtf16(text, result));
            return result;
        }

        /// @brief Preconverts native strings and indexes Unicode and annotation boundaries before publication.
        void indexText(const A::Node &node, Detail::AccessibilityTextCache &cache)
        {
            cache.name = utf16(node.name);
            cache.description = utf16(node.description);
            cache.help = utf16(node.helpText);
            cache.value = utf16(node.value);
            cache.language = utf16(node.language);
            cache.accessKey = utf16(node.accessKey);
            cache.shortcut = utf16(node.keyboardShortcut);
            if (!node.text)
            {
                return;
            }

            const auto text = node.text->utf8;
            cache.text = utf16(text);
            cache.textLanguage = utf16(node.text->language);
            for (const auto &annotation : node.text->annotations)
            {
                cache.annotationValues.push_back(utf16(annotation.value));
            }
            cache.formats = {0, static_cast<std::uint32_t>(text.size())};
            for (const auto &annotation : node.text->annotations)
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
            for (auto offset : boundaries)
            {
                cache.graphemes.push_back(static_cast<std::uint32_t>(offset));
            }

            std::uint32_t offset = 0, wideOffset = 0;
            cache.words.push_back(0);
            cache.lines.push_back(0);
            bool previousSpace = false;
            while (offset < text.size())
            {
                cache.scalars.push_back(offset);
                cache.utf16Offsets.push_back(wideOffset);
                const auto scalar = Unicode::Utf8::decodeScalar(text.substr(offset));
                const bool space = scalar.scalar == U' ' || scalar.scalar == U'\t' || scalar.scalar == U'\r' || scalar.scalar == U'\n' ||
                                   scalar.scalar == 0xA0 || scalar.scalar == 0x1680 || (scalar.scalar >= 0x2000 && scalar.scalar <= 0x200A) ||
                                   scalar.scalar == 0x2028 || scalar.scalar == 0x2029 || scalar.scalar == 0x202F || scalar.scalar == 0x205F ||
                                   scalar.scalar == 0x3000;
                if (previousSpace && !space && std::ranges::binary_search(cache.graphemes, offset))
                {
                    cache.words.push_back(offset);
                }
                previousSpace = space;
                offset += scalar.bytesConsumed;
                wideOffset += scalar.scalar > 0xFFFF ? 2U : 1U;
                if (scalar.scalar == U'\n' || scalar.scalar == 0x2028 || scalar.scalar == 0x2029)
                {
                    cache.lines.push_back(offset);
                }
            }
            cache.scalars.push_back(offset);
            cache.utf16Offsets.push_back(wideOffset);
            if (cache.words.back() != offset)
            {
                cache.words.push_back(offset);
            }
            if (cache.lines.back() != offset)
            {
                cache.lines.push_back(offset);
            }
        }
        // ------------------------------------------------------------
        // Semantic change notifications
        // ------------------------------------------------------------

        /// @brief Retains bounded committed-generation diffs, collapsing pressure into one current-tree invalidation.
        void queueDiff(
            Detail::AccessibilityRuntime &runtime,
            const std::shared_ptr<const Detail::PublishedSnapshot> &before,
            const std::shared_ptr<const Detail::PublishedSnapshot> &after)
        {
            std::lock_guard lock(runtime.notificationMutex);
            const auto invalidation = [&]
            {
                for (auto &notification : runtime.notifications)
                {
                    notification = {};
                }
                runtime.notificationHead = 0;
                runtime.notificationCount = 1;
                runtime.invalidationPending = true;
                ++runtime.collapsed;
                runtime.notifications[0] = {{A::NotificationKind::TreeInvalidated, after->storage.generation, after->storage.root}, before, after};
            };
            if (runtime.invalidationPending)
            {
                for (std::size_t i = 0; i < runtime.notificationCount; ++i)
                {
                    auto &notification = runtime.notifications[(runtime.notificationHead + i) % runtime.notifications.size()];
                    if (notification.notification.kind == A::NotificationKind::TreeInvalidated)
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
                if (runtime.invalidationPending)
                {
                    return;
                }
                if (runtime.notificationCount == runtime.notifications.size())
                {
                    invalidation();
                    return;
                }
                runtime.notifications[(runtime.notificationHead + runtime.notificationCount++) % runtime.notifications.size()] = {
                    {kind, after->storage.generation, id, 0, property},
                    before,
                    after};
            };
            if (!before)
            {
                push(A::NotificationKind::TreeInvalidated, after->storage.root);
                runtime.invalidationPending = true;
                return;
            }
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
            for (const auto &node : after->storage.nodes)
            {
                const auto *old = before->find(node.id);
                if (!old)
                {
                    structure = true;
                    continue;
                }
                if (node.parent != old->parent || !std::ranges::equal(node.children, old->children) || node.role != old->role)
                {
                    structure = true;
                }
                if (node.name != old->name)
                {
                    push(A::NotificationKind::PropertyChanged, node.id, A::PropertyKind::Name);
                }
                if (node.description != old->description)
                {
                    push(A::NotificationKind::PropertyChanged, node.id, A::PropertyKind::Description);
                }
                if (node.value != old->value || (node.rangeValue && (!old->rangeValue || node.rangeValue->value != old->rangeValue->value)))
                {
                    push(A::NotificationKind::PropertyChanged, node.id, A::PropertyKind::Value);
                }
                if (!optionalEqual(
                        node.rangeValue,
                        old->rangeValue,
                        [](const auto &a, const auto &b)
                        {
                            return a.minimum == b.minimum && a.maximum == b.maximum && a.smallChange == b.smallChange &&
                                   a.largeChange == b.largeChange;
                        }))
                {
                    push(A::NotificationKind::PropertyChanged, node.id, A::PropertyKind::Range);
                }
                if (!optionalEqual(
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
                if (node.states.flags != old->states.flags)
                {
                    push(A::NotificationKind::PropertyChanged, node.id, A::PropertyKind::State);
                }
                if (node.states.contains(A::State::Focused) && !old->states.contains(A::State::Focused))
                {
                    push(A::NotificationKind::FocusChanged, node.id);
                }
                if (node.states.contains(A::State::Selected) != old->states.contains(A::State::Selected) ||
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
                if (node.text.has_value() != old->text.has_value() || (node.text && old->text && node.text->utf8 != old->text->utf8))
                {
                    push(A::NotificationKind::TextChanged, node.id);
                }
                if (node.text && old->text &&
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
                if (!optionalEqual(node.geometry, old->geometry, geometryEqual))
                {
                    push(A::NotificationKind::PropertyChanged, node.id, A::PropertyKind::Geometry);
                }
                if (node.exposure != old->exposure)
                {
                    push(A::NotificationKind::PropertyChanged, node.id, A::PropertyKind::Exposure);
                }
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
                                       return x.range.begin == y.range.begin && x.range.end == y.range.end && x.kind == y.kind &&
                                              x.value == y.value && x.target == y.target;
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
                        return a.rowCount == b.rowCount && a.columnCount == b.columnCount && a.rowIndex == b.rowIndex &&
                               a.columnIndex == b.columnIndex && a.rowSpan == b.rowSpan && a.columnSpan == b.columnSpan &&
                               a.positionInSet == b.positionInSet && a.setSize == b.setSize && a.level == b.level;
                    });
                const bool virtualizationEqual = optionalEqual(
                    node.virtualization,
                    old->virtualization,
                    [](const auto &a, const auto &b)
                    {
                        return a.virtualized == b.virtualized && a.realized == b.realized && a.canRealize == b.canRealize &&
                               a.realizedChildCount == b.realizedChildCount && a.totalChildCount == b.totalChildCount;
                    });
                if (node.actions.flags != old->actions.flags || node.helpText != old->helpText || node.language != old->language ||
                    node.accessKey != old->accessKey || node.keyboardShortcut != old->keyboardShortcut || !relationsEqual || !textMetadataEqual ||
                    !collectionEqual || !virtualizationEqual || node.selection.has_value() != old->selection.has_value() ||
                    (node.selection && old->selection &&
                     (node.selection->multiSelectable != old->selection->multiSelectable ||
                      node.selection->selectionRequired != old->selection->selectionRequired ||
                      node.selection->activeNode != old->selection->activeNode)))
                {
                    push(A::NotificationKind::PropertyChanged, node.id, A::PropertyKind::Metadata);
                }
            }
            if (structure)
            {
                push(A::NotificationKind::StructureChanged, after->storage.root);
            }
        }
    } // namespace

    // ------------------------------------------------------------
    // Bridge activation
    // ------------------------------------------------------------

    Facade::Facade(Window &window) noexcept
        : window_(&window)
    {
    }

    IO::Types::Status Facade::enable(const A::Options &options) noexcept
    {
        if (!window_->isOpen())
        {
            return IO::makeStatus(IO::Types::ErrorCode::NotOpen);
        }
        if (!window_->ownedByCurrentThread())
        {
            return IO::makeStatus(IO::Types::ErrorCode::ResourceBusy);
        }
        if (!window_->supports(Types::Capability::Accessibility))
        {
            return IO::makeStatus(IO::Types::ErrorCode::Unsupported);
        }
        if (enabled())
        {
            return IO::makeStatus(IO::Types::ErrorCode::AlreadyOpen);
        }
        if (validate({}, options.limits).issue == A::ValidationIssue::InvalidLimits)
        {
            return IO::makeStatus(IO::Types::ErrorCode::InvalidArgument);
        }
        if (Detail::consumeFailure(TestHooks::FailurePoint::Allocation))
        {
            return IO::makeStatus(IO::Types::ErrorCode::OutOfMemory);
        }
        try
        {
            auto runtime = std::make_shared<Detail::AccessibilityRuntime>();
            runtime->limits = options.limits;
            runtime->ownerThread = std::this_thread::get_id();
            runtime->actions.resize(options.limits.maximumActionQueue);
            runtime->notifications.resize(options.limits.maximumNotificationQueue);

            auto &state = Detail::WindowAccess::accessibilityStateOwner(*window_);
            if (!state)
            {
                state.reset(new Detail::AccessibilityState());
            }
            auto *windowState = Detail::WindowAccess::state(*window_);
            auto status = Detail::Platform::enableAccessibility(*windowState, runtime);
            if (!status.ok())
            {
                return status;
            }

            state->runtime.store(runtime);
            windowState->accessibility = state.get();
            Detail::publishAccessibilityGeometry(*windowState);
            return {};
        }
        catch (const std::bad_alloc &)
        {
            return IO::makeStatus(IO::Types::ErrorCode::OutOfMemory);
        }
        catch (...)
        {
            return IO::makeStatus(IO::Types::ErrorCode::SizeLimitExceeded);
        }
    }

    IO::Types::Status Facade::disable() noexcept
    {
        const auto runtime = runtimeFor(window_);
        if (!runtime || !runtime->live.load())
        {
            return {};
        }
        if (runtime->ownerThread != std::this_thread::get_id())
        {
            return IO::makeStatus(IO::Types::ErrorCode::ResourceBusy);
        }
        if (auto *state = Detail::WindowAccess::state(*window_))
        {
            Detail::closeAccessibility(*state);
        }
        return {};
    }

    bool Facade::enabled() const noexcept
    {
        const auto runtime = runtimeFor(window_);
        return runtime && runtime->live.load();
    }

    A::Features Facade::features() const noexcept
    {
        const auto runtime = runtimeFor(window_);
        return runtime && runtime->live.load() ? runtime->features : A::Features{};
    }
    // ------------------------------------------------------------
    // Snapshot publication and inspection
    // ------------------------------------------------------------

    IO::Types::Status Facade::publish(const A::SnapshotView &snapshot) noexcept
    {
        const auto runtime = runtimeFor(window_);
        if (!runtime || !runtime->live.load())
        {
            return IO::makeStatus(IO::Types::ErrorCode::NotOpen);
        }
        const auto validation = validate(snapshot, runtime->limits);
        if (!validation.ok())
        {
            return validation.status;
        }
        if (Detail::consumeFailure(TestHooks::FailurePoint::Allocation))
        {
            return IO::makeStatus(IO::Types::ErrorCode::OutOfMemory);
        }
        try
        {
            auto candidate = std::make_shared<Detail::PublishedSnapshot>();
            candidate->storage.generation = snapshot.generation;
            candidate->storage.root = snapshot.root;
            candidate->storage.owned.reserve(snapshot.nodes.size());
            candidate->storage.nodes.reserve(snapshot.nodes.size());
            candidate->index.reserve(snapshot.nodes.size());
            candidate->textCache.resize(snapshot.nodes.size());

            for (std::size_t i = 0; i < snapshot.nodes.size(); ++i)
            {
                candidate->storage.owned.push_back(std::make_shared<Detail::OwnedAccessibilityNode>(snapshot.nodes[i]));
                candidate->storage.nodes.push_back(candidate->storage.owned.back()->node);
                candidate->index.emplace_back(snapshot.nodes[i].id, i);
                if (snapshot.nodes[i].states.contains(A::State::Focused))
                {
                    candidate->focusedNode = snapshot.nodes[i].id;
                }
                indexText(snapshot.nodes[i], candidate->textCache[i]);
            }
            std::ranges::sort(candidate->index);

            auto *state = Detail::WindowAccess::accessibilityState(*window_);
            std::lock_guard publication(state->publicationMutex);
            if (!runtime->live.load())
            {
                return IO::makeStatus(IO::Types::ErrorCode::NotOpen);
            }
            if (snapshot.generation <= state->lastGeneration)
            {
                return IO::makeStatus(IO::Types::ErrorCode::InvalidArgument);
            }
            const auto before = runtime->snapshot.load();
            if (before && before->storage.root != snapshot.root)
            {
                return IO::makeStatus(IO::Types::ErrorCode::InvalidArgument);
            }

            const auto status = Detail::Platform::prepareAccessibilitySnapshot(runtime, *candidate);
            if (!status.ok())
            {
                return status;
            }
            if (!runtime->live.load())
            {
                return IO::makeStatus(IO::Types::ErrorCode::NotOpen);
            }

            runtime->snapshot.store(candidate);
            state->lastGeneration = snapshot.generation;
            queueDiff(*runtime, before, candidate);
            Detail::Platform::wakeAccessibility(*runtime);
            return {};
        }
        catch (const std::bad_alloc &)
        {
            return IO::makeStatus(IO::Types::ErrorCode::OutOfMemory);
        }
        catch (...)
        {
            return IO::makeStatus(IO::Types::ErrorCode::SizeLimitExceeded);
        }
    }

    IO::Types::Status Facade::publish(const SnapshotBuilder &snapshot) noexcept
    {
        return publish(snapshot.view());
    }

    A::SnapshotInfo Facade::snapshotInfo() const noexcept
    {
        return readSnapshot().info();
    }

    SnapshotReader Facade::readSnapshot() const noexcept
    {
        const auto runtime = runtimeFor(window_);
        return SnapshotReader(runtime && runtime->live.load() ? runtime->snapshot.load() : nullptr);
    }

    IO::Types::Status Facade::copySnapshot(SnapshotBuilder &destination) const noexcept
    {
        const auto reader = readSnapshot();
        if (!reader.isValid())
        {
            return IO::makeStatus(IO::Types::ErrorCode::NotOpen);
        }

        SnapshotBuilder copy(destination.limits());
        auto status = copy.setGeneration(reader.info().generation);
        if (!status.ok())
        {
            return status;
        }
        status = copy.setRoot(reader.info().root);
        if (!status.ok())
        {
            return status;
        }
        for (const auto &node : reader.view().nodes)
        {
            status = copy.addNode(node);
            if (!status.ok())
            {
                return status;
            }
        }

        destination = std::move(copy);
        return {};
    }
    // ------------------------------------------------------------
    // Application actions and announcements
    // ------------------------------------------------------------

    bool Facade::popAction(A::ActionRequest &outAction) noexcept
    {
        const auto runtime = runtimeFor(window_);
        if (!runtime || runtime->ownerThread != std::this_thread::get_id())
        {
            return false;
        }
        std::lock_guard lock(runtime->actionMutex);
        if (!runtime->live.load() || runtime->actionCount == 0)
        {
            return false;
        }
        auto &slot = runtime->actions[runtime->actionHead];
        outAction = std::move(slot);
        slot = {};
        runtime->actionHead = (runtime->actionHead + 1) % runtime->actions.size();
        --runtime->actionCount;
        return true;
    }

    A::QueueInfo Facade::queueInfo() const noexcept
    {
        const auto runtime = runtimeFor(window_);
        if (!runtime)
        {
            return {};
        }
        A::QueueInfo result;
        {
            std::lock_guard lock(runtime->actionMutex);
            result.actions = {
                static_cast<std::uint32_t>(runtime->actions.size()),
                static_cast<std::uint32_t>(runtime->actionCount),
                runtime->rejected};
        }
        {
            std::lock_guard lock(runtime->notificationMutex);
            result.notifications = {
                static_cast<std::uint32_t>(runtime->notifications.size()),
                static_cast<std::uint32_t>(runtime->notificationCount),
                runtime->collapsed,
                runtime->invalidationPending};
        }
        return result;
    }

    IO::Types::Status Facade::announce(const A::AnnouncementView &announcement) noexcept
    {
        const auto runtime = runtimeFor(window_);
        if (!runtime || !runtime->live.load())
        {
            return IO::makeStatus(IO::Types::ErrorCode::NotOpen);
        }
        if (!runtime->features.supports(A::Feature::Announcements))
        {
            return IO::makeStatus(IO::Types::ErrorCode::Unsupported);
        }
        if (announcement.priority > A::AnnouncementPriority::High ||
            announcement.text.size() + announcement.language.size() > runtime->limits.maximumAnnouncementBytes)
        {
            return IO::makeStatus(IO::Types::ErrorCode::InvalidArgument);
        }
        if (Unicode::Utf8::validate(announcement.text).outcome != Unicode::Types::ValidationOutcome::Valid ||
            Unicode::Utf8::validate(announcement.language).outcome != Unicode::Types::ValidationOutcome::Valid)
        {
            return IO::makeStatus(IO::Types::ErrorCode::EncodingFailed);
        }
        const auto snapshot = runtime->snapshot.load();
        const auto id = announcement.node != 0 ? announcement.node : snapshot ? snapshot->storage.root : 0;
        if (!snapshot || !snapshot->find(id))
        {
            return IO::makeStatus(IO::Types::ErrorCode::InvalidArgument);
        }
        if (!announcement.language.empty() && announcement.language != snapshot->find(id)->language &&
            !runtime->features.supports(A::Feature::AnnouncementLanguage))
        {
            return IO::makeStatus(IO::Types::ErrorCode::Unsupported);
        }
        try
        {
            Detail::AccessibilityPendingNotification pending{
                {A::NotificationKind::LiveRegionChanged, snapshot->storage.generation, id},
                {},
                snapshot,
                utf16(announcement.text),
                announcement.priority};
            std::lock_guard lock(runtime->notificationMutex);
            if (!runtime->live.load())
            {
                return IO::makeStatus(IO::Types::ErrorCode::NotOpen);
            }
            if (runtime->notificationCount == runtime->notifications.size())
            {
                return IO::makeStatus(IO::Types::ErrorCode::SizeLimitExceeded);
            }
            runtime->notifications[(runtime->notificationHead + runtime->notificationCount++) % runtime->notifications.size()] = std::move(pending);
            Detail::Platform::wakeAccessibility(*runtime);
            return {};
        }
        catch (...)
        {
            return IO::makeStatus(IO::Types::ErrorCode::OutOfMemory);
        }
    }
} // namespace GameWIP::Desktop::Accessibility
