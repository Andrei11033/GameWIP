/// @file accessibility_transport.cpp
/// @brief Thread-safe accessibility lifecycle and bounded event transport.

#include "desktop/internal/accessibility_state.h"
#include "desktop/platform/window.h"
#include "desktop/internal/desktop_test_hooks.h"
#include "unicode/unicode.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <mutex>
#include <thread>
#include <utility>

namespace GameWIP::Desktop::Internal
{
    /// ------------------------------------------------------------
    /// State ownership and host publication
    /// ------------------------------------------------------------

    void AccessibilityStateDeleter::operator()(AccessibilityState *state) const noexcept
    {
        delete state;
    }

    void AccessibilityRuntime::publishGeometry(const AccessibilityHostGeometry &geometry) noexcept
    {
        // Mark the update while the fields are being written.
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
        for(unsigned attempt = 0; attempt < 8; ++attempt)
        {
            const auto sequence = geometrySequence.load();

            if((sequence & 1U) != 0)
            {
                continue;
            }
            geometry = {x.load(), y.load(), width.load(), height.load(), scaleX.load(), scaleY.load(), visible.load(), focused.load()};

            if(sequence == geometrySequence.load())
            {
                return true;
            }
        }

        return false;
    }

    void publishAccessibilityGeometry(WindowState &window) noexcept
    {
        if(!window.accessibility)
        {
            return;
        }
        const auto runtime = window.accessibility->runtime.load();

        if(!runtime || !runtime->live.load())
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

        if(changed)
        {
            invalidateAccessibilityHost(window, !before.focused && after.focused);
        }
    }

    void invalidateAccessibilityHost(WindowState &window, bool focusGained) noexcept
    {
        if(!window.accessibility)
        {
            return;
        }
        const auto runtime = window.accessibility->runtime.load();

        if(!runtime || !runtime->live.load())
        {
            return;
        }
        std::lock_guard lock(runtime->notificationMutex);
        const auto snapshot = runtime->snapshot.load();

        if(!snapshot || !runtime->live.load())
        {
            return;
        }

        // Keep one invalidation current while the queue is coalesced.
        if(runtime->invalidationPending)
        {
            for(std::size_t i = 0; i < runtime->notificationCount; ++i)
            {
                auto &notification = runtime->notifications[(runtime->notificationHead + i) % runtime->notifications.size()];

                if(notification.notification.kind == A::NotificationKind::TreeInvalidated)
                {
                    notification.notification.generation = snapshot->storage.generation;
                    notification.after = snapshot;
                    break;
                }
            }
        }
        else
        {
            if(runtime->notificationCount == runtime->notifications.size())
            {
                for(auto &notification : runtime->notifications)
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

        // Append focus after the tree invalidation so clients see the current target.
        if(focusGained && snapshot->focusedNode != 0)
        {
            if(runtime->notificationCount < runtime->notifications.size())
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
        // Wake the native pump for the queued notifications.
        Platform::wakeAccessibility(*runtime);
    }

    void closeAccessibility(WindowState &window) noexcept
    {
        if(!window.accessibility)
        {
            return;
        }
        auto &state = *window.accessibility;
        const auto runtime = state.runtime.load();

        if(!runtime || !runtime->live.exchange(false))
        {
            return;
        }

        // Stop publication before releasing snapshots and queued work.
        std::lock_guard publication(state.publicationMutex);
        runtime->snapshot.store({});

        // Discard actions that can no longer reach the application.
        {
            std::lock_guard actions(runtime->actionMutex);

            for(auto &action : runtime->actions)
            {
                action = {};
            }
            runtime->actionCount = 0;
            runtime->actionHead = 0;
        }

        // Release retained generations and pending announcements.
        {
            std::lock_guard notifications(runtime->notificationMutex);

            for(auto &notification : runtime->notifications)
            {
                notification = {};
            }
            runtime->notificationCount = 0;
            runtime->notificationHead = 0;
            runtime->invalidationPending = false;
        }

        // Detach native providers after shared queues are empty.
        Platform::detachAccessibility(*runtime);
        runtime->nativeWindow.store(0);
    }

    /// ------------------------------------------------------------
    /// Bounded action and notification transport
    /// ------------------------------------------------------------

    A::ActionResult submitAccessibilityAction(AccessibilityRuntime &runtime, A::ActionRequest request) noexcept
    {
        // Validate the request against the current published tree.
        if(!runtime.live.load())
        {
            return {A::ActionAcceptance::Closed};
        }
        const auto snapshot = runtime.snapshot.load();
        const auto *node = snapshot ? snapshot->find(request.node) : nullptr;

        if(!node)
        {
            return {A::ActionAcceptance::Invalid};
        }

        if(request.action >= A::ActionKind::Count || !node->actions.contains(request.action))
        {
            return {A::ActionAcceptance::Unsupported};
        }

        if(node->states.contains(A::State::Disabled) || !runtime.hostEnabled.load())
        {
            return {A::ActionAcceptance::Rejected};
        }

        if(!std::isfinite(request.value) || !std::isfinite(request.point.x) || !std::isfinite(request.point.y) ||
           !std::isfinite(request.scroll.horizontal) || !std::isfinite(request.scroll.vertical) ||
           request.text.size() > runtime.limits.maximumActionTextBytes || request.selection.size() > runtime.limits.maximumSelectionRangesPerAction ||
           Unicode::Utf8::validate(request.text).outcome != Unicode::Types::ValidationOutcome::Valid)
        {
            return {A::ActionAcceptance::Invalid};
        }

        if(request.action == A::ActionKind::SetValue && node->states.contains(A::State::ReadOnly))
        {
            return {A::ActionAcceptance::Rejected};
        }

        if(request.action == A::ActionKind::SetValue && node->rangeValue &&
           (request.value < node->rangeValue->minimum || request.value > node->rangeValue->maximum))
        {
            return {A::ActionAcceptance::Invalid};
        }

        if(request.scroll.unit > A::ScrollUnit::Step || request.selectionOperation > A::SelectionOperation::Remove)
        {
            return {A::ActionAcceptance::Invalid};
        }

        // Validate text ranges against cached grapheme boundaries.
        if(request.action == A::ActionKind::SetTextSelection || request.range || !request.selection.empty())
        {
            if(!node->text)
            {
                return {A::ActionAcceptance::Unsupported};
            }
            const auto &boundaries = snapshot->textCache[snapshot->findIndex(node->id)].graphemes;
            const auto valid = [&](A::TextRange range)
            {
                return range.begin <= range.end && std::ranges::binary_search(boundaries, range.begin) &&
                       std::ranges::binary_search(boundaries, range.end);
            };
            bool invalidSelection = false;

            for(const auto range : request.selection)
            {
                if(!valid(range))
                {
                    invalidSelection = true;
                    break;
                }
            }

            if((request.range && !valid(*request.range)) || invalidSelection)
            {
                return {A::ActionAcceptance::Invalid};
            }
        }

        std::lock_guard lock(runtime.actionMutex);

        if(!runtime.live.load())
        {
            return {A::ActionAcceptance::Closed};
        }

        if(runtime.nextRequest == 0)
        {
            return {A::ActionAcceptance::Rejected};
        }

        if(request.observedGeneration == 0)
        {
            request.observedGeneration = snapshot->storage.generation;
        }

        if(request.observedGeneration > snapshot->storage.generation)
        {
            return {A::ActionAcceptance::Invalid};
        }
        request.requestId = runtime.nextRequest++;
        const A::ActionResult result{A::ActionAcceptance::Accepted, request.requestId, request.observedGeneration};

        // Coalesce replaceable requests without crossing durable work.
        // Replace only the tail: crossing a durable request would reorder its observable effects.
        const bool coalescible = request.action == A::ActionKind::Focus || request.action == A::ActionKind::SetValue ||
                                 request.action == A::ActionKind::ScrollTo || request.action == A::ActionKind::Realize;

        if(coalescible && runtime.actionCount != 0)
        {
            auto &tail = runtime.actions[(runtime.actionHead + runtime.actionCount - 1) % runtime.actions.size()];

            if(tail.node == request.node && tail.action == request.action)
            {
                tail = std::move(request);
                Platform::wakeAccessibility(runtime);
                return result;
            }
        }

        // Preserve the queue order and report pressure when no slot remains.
        if(runtime.actionCount == runtime.actions.size())
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
        if(!window.accessibility)
        {
            return;
        }
        const auto runtime = window.accessibility->runtime.load();

        if(!runtime || runtime->ownerThread != std::this_thread::get_id())
        {
            return;
        }

        // A bounded batch prevents reentrant publications from extending a pump indefinitely.
        const std::size_t capacity = runtime->notifications.size();

        for(std::size_t i = 0; i < capacity; ++i)
        {
            AccessibilityPendingNotification notification;
            {
                std::lock_guard lock(runtime->notificationMutex);

                if(!runtime->live.load() || runtime->notificationCount == 0)
                {
                    return;
                }
                auto &slot = runtime->notifications[runtime->notificationHead];
                notification = std::move(slot);
                slot = {};
                runtime->notificationHead = (runtime->notificationHead + 1) % capacity;
                --runtime->notificationCount;

                if(notification.notification.kind == A::NotificationKind::TreeInvalidated)
                {
                    runtime->invalidationPending = false;
                }
            }

            Platform::deliverAccessibilityNotification(*runtime, notification);
        }
    }
} // namespace GameWIP::Desktop::Internal

namespace GameWIP::Desktop
{
    Accessibility::Facade Window::accessibility() noexcept
    {
        return Accessibility::Facade(*this);
    }
} // namespace GameWIP::Desktop

#if DESKTOP_INTERNAL_TEST_HOOKS
namespace GameWIP::Desktop::Internal::TestHooks
{
    Types::Accessibility::ActionResult submitAccessibilityAction(Window &window, Types::Accessibility::ActionRequest request) noexcept
    {
        const auto *state = Internal::WindowAccess::accessibilityState(window);
        const auto runtime = state ? state->runtime.load() : nullptr;
        return runtime ? Internal::submitAccessibilityAction(*runtime, std::move(request))
                       : Types::Accessibility::ActionResult{Types::Accessibility::ActionAcceptance::Closed};
    }

    bool popAccessibilityNotification(Window &window, Types::Accessibility::Notification &out) noexcept
    {
        const auto *state = Internal::WindowAccess::accessibilityState(window);
        const auto runtime = state ? state->runtime.load() : nullptr;

        if(!runtime || runtime->ownerThread != std::this_thread::get_id())
        {
            return false;
        }
        std::lock_guard lock(runtime->notificationMutex);

        if(!runtime->live.load() || runtime->notificationCount == 0)
        {
            return false;
        }
        auto &slot = runtime->notifications[runtime->notificationHead];
        out = slot.notification;
        slot = {};
        runtime->notificationHead = (runtime->notificationHead + 1) % runtime->notifications.size();
        --runtime->notificationCount;

        if(out.kind == Types::Accessibility::NotificationKind::TreeInvalidated)
        {
            runtime->invalidationPending = false;
        }

        return true;
    }
} // namespace GameWIP::Desktop::Internal::TestHooks
#endif
