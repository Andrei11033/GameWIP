/// @file win32_accessibility_lifecycle.cpp
/// @brief Publishes UI Automation snapshots and delivers native accessibility events.

#include "desktop/internal/results.h"
#include "desktop/platform/win32/internal/win32_accessibility_helpers.h"
#include "desktop/platform/win32/internal/win32_accessibility_provider.h"
#include "base/platform/win32/dynamic_library.h"

#include <algorithm>
#include <new>

namespace GameWIP::Desktop::Platform
{
    namespace
    {
        /// ------------------------------------------------------------
        /// Native property notifications
        /// ------------------------------------------------------------

        void propertyEvent(
            const AccessibilityPendingNotification &event,
            IRawElementProviderSimple *provider,
            PROPERTYID id,
            const std::shared_ptr<AccessibilityRuntime> &runtime) noexcept
        {
            if(!event.before || !event.after)
            {
                return;
            }

            // Build queries against the snapshots on each side of the change.
            const auto beforeIndex = event.before->findIndex(event.notification.node), afterIndex = event.after->findIndex(event.notification.node);

            if(beforeIndex >= event.before->storage.nodes.size() || afterIndex >= event.after->storage.nodes.size())
            {
                return;
            }
            UiaQuery before{runtime, event.before, &event.before->storage.nodes[beforeIndex], beforeIndex};
            UiaQuery after{runtime, event.after, &event.after->storage.nodes[afterIndex], afterIndex};
            AccessibilityHostGeometry host;

            if(runtime->readGeometry(host))
            {
                before.host = after.host = host;
            }
            auto protectedBefore = *before.node;
            auto protectedAfter = *after.node;
            const auto current = runtime->snapshot.load();
            const auto *currentNode = current ? current->find(after.node->id) : nullptr;

            if(!currentNode)
            {
                return;
            }

            // Apply the latest exposure rules to both values before announcing a change.
            if(after.node->exposure == A::Exposure::RedactContent || currentNode->exposure == A::Exposure::RedactContent)
            {
                protectedBefore.exposure = A::Exposure::RedactContent;
            }

            if(currentNode->exposure == A::Exposure::RedactContent)
            {
                protectedAfter.exposure = A::Exposure::RedactContent;
            }

            if(valueHidden(*after.node) || valueHidden(*currentNode))
            {
                protectedBefore.states.flags |= static_cast<std::uint64_t>(A::State::Sensitive);
            }

            if(valueHidden(*currentNode))
            {
                protectedAfter.states.flags |= static_cast<std::uint64_t>(A::State::Sensitive);
            }
            before.node = &protectedBefore;
            after.node = &protectedAfter;
            VARIANT oldValue{}, newValue{};

            // Raise the event only when both snapshot values can be read.
            if(SUCCEEDED(property(before, id, &oldValue)) && SUCCEEDED(property(after, id, &newValue)))
            {
                static_cast<void>(UiaRaiseAutomationPropertyChangedEvent(provider, id, oldValue, newValue));
            }
            VariantClear(&oldValue);
            VariantClear(&newValue);
        }
    } // namespace

    /// ------------------------------------------------------------
    /// Backend activation and transactional provider preparation
    /// ------------------------------------------------------------

    GameWIP::Status::Types::Status enableAccessibility(WindowState &window, const std::shared_ptr<AccessibilityRuntime> &runtime) noexcept
    {
        try
        {
            const auto *data = static_cast<const WindowData *>(window.platform.get());

            if(!data || !data->handle)
            {
                return GameWIP::Desktop::Internal::statusFor(GameWIP::Status::Types::Code::NotOpen);
            }

            // Allocate the native state and bounded text-range pool.
            auto native = std::make_shared<UiaState>();
            native->ranges.reserve(runtime->limits.maximumNativeTextRanges);

            for(std::uint32_t i = 0; i < runtime->limits.maximumNativeTextRanges; ++i)
            {
                native->ranges.push_back(std::make_unique<TextRangeProvider>());
            }

            // Load the optional announcement entry point.
            if(const auto module = GetModuleHandleW(L"uiautomationcore.dll"))
            {
                native->raiseNotification = Base::Platform::Win32::loadProcedure<RaiseNotification>(module, "UiaRaiseNotificationEvent");
            }

            // Publish host linkage and supported accessibility features.
            runtime->nativeThread = data->ownerThreadId;
            runtime->hostEnabled.store(IsWindowEnabled(data->handle) != FALSE);
            runtime->nativeWindow.store(reinterpret_cast<std::uintptr_t>(data->handle));
            runtime->native = std::move(native);
            runtime->features.flags = (std::uint64_t{1} << static_cast<unsigned>(A::Feature::Count)) - 1;
            runtime->features.flags &= ~(std::uint64_t{1} << static_cast<unsigned>(A::Feature::AnnouncementLanguage));

            if(!static_cast<UiaState *>(runtime->native.get())->raiseNotification)
            {
                runtime->features.flags &= ~(std::uint64_t{1} << static_cast<unsigned>(A::Feature::Announcements));
            }

            return {};
        }
        catch(const std::bad_alloc &)
        {
            return GameWIP::Desktop::Internal::statusFor(GameWIP::Status::Types::Code::OutOfMemory);
        }
        catch(...)
        {
            return GameWIP::Desktop::Internal::statusFor(GameWIP::Status::Types::Code::SizeLimitExceeded);
        }
    }

    GameWIP::Status::Types::Status prepareAccessibilitySnapshot(const std::shared_ptr<AccessibilityRuntime> &runtime, PublishedSnapshot &snapshot) noexcept
    {
        auto &native = *static_cast<UiaState *>(runtime->native.get());
        try
        {
            std::size_t missing = 0;

            for(const auto &node : snapshot.storage.nodes)
            {
                if(!native.providers.contains(node.id))
                {
                    ++missing;
                }
            }

            // Enforce the provider limit before allocating snapshot state.
            if(missing > runtime->limits.maximumNativeProviders - native.providers.size())
            {
                return GameWIP::Desktop::Internal::statusFor(GameWIP::Status::Types::Code::SizeLimitExceeded);
            }

            auto result = std::make_shared<UiaSnapshot>();
            result->providers.reserve(snapshot.storage.nodes.size());
            result->layout.resize(snapshot.storage.nodes.size());
            result->annotationSources.resize(snapshot.storage.nodes.size(), {snapshot.storage.nodes.size(), 0});

            // Index annotation owners for constant-time node lookup.
            for(std::size_t i = 0; i < snapshot.storage.nodes.size(); ++i)
            {
                if(const auto &text = snapshot.storage.nodes[i].text)
                {
                    for(std::size_t j = 0; j < text->annotations.size(); ++j)
                    {
                        if(const auto target = text->annotations[j].target; target != 0)
                        {
                            auto &reference = result->annotationSources[snapshot.findIndex(target)];

                            if(reference.first == snapshot.storage.nodes.size())
                            {
                                reference = {i, j};
                            }
                        }
                    }
                }
            }

            // Prepare new provider identities under temporary ownership.
            struct Additions
            {
                std::map<A::NodeId, NodeProvider *> values;
                ~Additions()
                {
                    for(auto [id, provider] : values)
                    {
                        static_cast<void>(id);
                        provider->Release();
                    }
                }
            } additions;

            for(const auto &node : snapshot.storage.nodes)
            {
                if(!native.providers.contains(node.id))
                {
                    auto *provider = new NodeProvider(runtime, node.id, node.id == snapshot.storage.root);
                    try
                    {
                        additions.values.emplace(node.id, provider);
                    }
                    catch(...)
                    {
                        provider->Release();
                        throw;
                    }
                }
            }

            // Build cached layout and tree traversal order.
            std::vector<A::NodeId> pending;
            pending.reserve(snapshot.storage.nodes.size());
            pending.push_back(snapshot.storage.root);
            std::vector<std::size_t> traversal;
            traversal.reserve(snapshot.storage.nodes.size());
            std::uint32_t ordinal = 0;

            while(!pending.empty())
            {
                const auto id = pending.back();
                pending.pop_back();
                const auto index = snapshot.findIndex(id);
                const auto &node = snapshot.storage.nodes[index];
                auto &layout = result->layout[index];
                layout.ordinal = ordinal++;
                layout.subtreeEnd = ordinal;
                traversal.push_back(index);

                if(node.parent != 0)
                {
                    const auto parentIndex = snapshot.findIndex(node.parent);
                    const auto &parent = result->layout[parentIndex];
                    const auto &parentNode = snapshot.storage.nodes[parentIndex];
                    layout.clip = parent.clip;
                    layout.visible = parent.visible;
                    layout.selectionContainer = parentNode.selection ? parentNode.id : parent.selectionContainer;
                    layout.containingGrid = parentNode.role == A::Role::Table && parentNode.collection ? parentNode.id : parent.containingGrid;
                }
                layout.visible = layout.visible && !node.states.contains(A::State::Invisible);

                if(node.geometry)
                {
                    layout.bounds = transformed(*node.geometry);
                    layout.visible = layout.visible && node.geometry->visible;
                    layout.order = node.geometry->hitTestOrder;

                    if(node.geometry->clippingBounds)
                    {
                        layout.clip = layout.clip ? intersection(*layout.clip, *node.geometry->clippingBounds) : node.geometry->clippingBounds;
                    }
                }

                for(auto child = node.children.rbegin(); child != node.children.rend(); ++child)
                {
                    pending.push_back(*child);
                }
            }

            // Close each subtree interval from the last descendant upward.
            for(auto index = traversal.rbegin(); index != traversal.rend(); ++index)
            {
                if(const auto parent = snapshot.storage.nodes[*index].parent; parent != 0)
                {
                    auto &layout = result->layout[snapshot.findIndex(parent)];
                    layout.subtreeEnd = std::max(layout.subtreeEnd, result->layout[*index].subtreeEnd);
                }
            }

            // Transfer provider ownership only after fallible snapshot work is complete.
            native.providers.merge(additions.values);

            // Retain providers and cached text for the published snapshot.
            for(const auto &node : snapshot.storage.nodes)
            {
                auto *provider = static_cast<IRawElementProviderSimple *>(native.providers.find(node.id)->second);
                provider->AddRef();
                result->providers.push_back(provider);
            }

            snapshot.native = std::move(result);
            return {};
        }
        catch(const std::bad_alloc &)
        {
            return GameWIP::Desktop::Internal::statusFor(GameWIP::Status::Types::Code::OutOfMemory);
        }
        catch(...)
        {
            return GameWIP::Desktop::Internal::statusFor(GameWIP::Status::Types::Code::SizeLimitExceeded);
        }
    }

    /// ------------------------------------------------------------
    /// Native lifetime and notification delivery
    /// ------------------------------------------------------------

    void detachAccessibility(AccessibilityRuntime &runtime) noexcept
    {
        auto *native = static_cast<UiaState *>(runtime.native.get());

        if(!native)
        {
            return;
        }

        if(runtime.nativeThread == GetCurrentThreadId())
        {
            static_cast<void>(UiaReturnRawElementProvider(reinterpret_cast<HWND>(runtime.nativeWindow.load()), 0, 0, nullptr));
        }

        for(auto [id, provider] : native->providers)
        {
            static_cast<void>(id);
            static_cast<void>(UiaDisconnectProvider(static_cast<IRawElementProviderSimple *>(provider)));
            provider->Release();
        }
        native->providers.clear();
    }

    void wakeAccessibility(const AccessibilityRuntime &runtime) noexcept
    {
        if(const auto message = wakeMessage())
        {
            static_cast<void>(PostThreadMessageW(runtime.nativeThread, message, 0, 0));
        }
    }

    bool accessibilityGetObject(WindowState &state, HWND hwnd, WPARAM wParam, LPARAM lParam, LRESULT &out) noexcept
    {
        if(static_cast<LONG>(lParam) != UiaRootObjectId || !state.accessibility)
        {
            return false;
        }
        const auto runtime = state.accessibility->runtime.load();

        if(!runtime || !runtime->live.load())
        {
            return false;
        }
        const auto snapshot = runtime->snapshot.load();

        if(!snapshot)
        {
            return false;
        }
        auto *provider = providerFor(*snapshot, snapshot->storage.root);

        if(!provider)
        {
            return false;
        }

        static_cast<WindowData *>(state.platform.get())->accessibilityExposed = true;
        out = UiaReturnRawElementProvider(hwnd, wParam, lParam, provider);
        return true;
    }

    void accessibilityWindowDestroyed(WindowState &state, HWND hwnd) noexcept
    {
        if(auto *data = static_cast<WindowData *>(state.platform.get()); data && data->accessibilityExposed)
        {
            static_cast<void>(UiaReturnRawElementProvider(hwnd, 0, 0, nullptr));
            data->accessibilityExposed = false;
        }
        closeAccessibility(state);
    }

    void deliverAccessibilityNotification(AccessibilityRuntime &runtime, const AccessibilityPendingNotification &event) noexcept
    {
        // Ignore stale notifications and work no accessibility client requested.
        if(!runtime.live.load() || !event.after || !UiaClientsAreListening())
        {
            return;
        }

        // Resolve the provider and node retained by this notification.
        auto *provider = providerFor(*event.after, event.notification.node);

        if(!provider)
        {
            return;
        }

        const auto *node = event.after->find(event.notification.node);

        if(!node)
        {
            return;
        }

        // Suppress events for nodes removed from the current tree.
        const auto currentSnapshot = runtime.snapshot.load();
        const auto *currentNode = currentSnapshot ? currentSnapshot->find(node->id) : nullptr;

        if(!currentNode)
        {
            return;
        }

        const auto &notification = event.notification;

        switch(notification.kind)
        {
        case A::NotificationKind::TreeInvalidated:
        case A::NotificationKind::StructureChanged:
            static_cast<void>(UiaRaiseStructureChangedEvent(provider, StructureChangeType_ChildrenInvalidated, nullptr, 0));
            break;
        case A::NotificationKind::FocusChanged:
        {
            AccessibilityHostGeometry host;

            if(runtime.readGeometry(host) && host.focused && currentNode->states.contains(A::State::Focused))
            {
                static_cast<void>(UiaRaiseAutomationEvent(provider, UIA_AutomationFocusChangedEventId));
            }
            break;
        }
        case A::NotificationKind::SelectionChanged:
            if(notification.property == A::PropertyKind::Text)
            {
                if(!textHidden(*node) && !textHidden(*currentNode))
                {
                    static_cast<void>(UiaRaiseAutomationEvent(provider, UIA_Text_TextSelectionChangedEventId));
                }
            }
            else if(node->selection)
            {
                static_cast<void>(UiaRaiseAutomationEvent(provider, UIA_Selection_InvalidatedEventId));
            }
            else if(node->states.contains(A::State::Selectable))
            {
                const auto containerId =
                    static_cast<const UiaSnapshot *>(event.after->native.get())->layout[event.after->findIndex(node->id)].selectionContainer;
                const auto *container = event.after->find(containerId);
                const auto eventId = !node->states.contains(A::State::Selected) ? UIA_SelectionItem_ElementRemovedFromSelectionEventId
                                     : container && container->selection && container->selection->multiSelectable
                                         ? UIA_SelectionItem_ElementAddedToSelectionEventId
                                         : UIA_SelectionItem_ElementSelectedEventId;
                static_cast<void>(UiaRaiseAutomationEvent(provider, eventId));
            }
            else
            {
                static_cast<void>(UiaRaiseStructureChangedEvent(provider, StructureChangeType_ChildrenInvalidated, nullptr, 0));
            }
            break;
        case A::NotificationKind::TextChanged:
            if(!textHidden(*node) && !textHidden(*currentNode))
            {
                static_cast<void>(UiaRaiseAutomationEvent(provider, UIA_Text_TextChangedEventId));
            }
            break;
        case A::NotificationKind::LiveRegionChanged:
        {
            auto *native = static_cast<UiaState *>(runtime.native.get());

            if(!native->raiseNotification || textHidden(*node) || textHidden(*currentNode))
            {
                break;
            }
            BSTR text = nullptr, activity = nullptr;

            if(SUCCEEDED(stringValue(event.announcement, &text)) && SUCCEEDED(stringValue(u"GameWIP.Announcement", &activity)))
            {
                static_cast<void>(native->raiseNotification(
                    provider,
                    NotificationKind_Other,
                    event.priority == A::AnnouncementPriority::High  ? NotificationProcessing_ImportantAll
                    : event.priority == A::AnnouncementPriority::Low ? NotificationProcessing_MostRecent
                                                                     : NotificationProcessing_All,
                    text,
                    activity));
            }
            SysFreeString(text);
            SysFreeString(activity);
            break;
        }
        case A::NotificationKind::PropertyChanged:
        {
            // Values exposed in property events are subject to the same redaction as direct queries.
            auto owningRuntime = static_cast<NodeProvider *>(provider)->query().runtime;

            if(!owningRuntime)
            {
                break;
            }

            switch(notification.property)
            {
            case A::PropertyKind::Name:
                propertyEvent(event, provider, UIA_NamePropertyId, owningRuntime);
                break;
            case A::PropertyKind::Description:
                propertyEvent(event, provider, UIA_FullDescriptionPropertyId, owningRuntime);
                break;
            case A::PropertyKind::Value:
                propertyEvent(event, provider, UIA_ValueValuePropertyId, owningRuntime);
                propertyEvent(event, provider, UIA_RangeValueValuePropertyId, owningRuntime);
                break;
            case A::PropertyKind::Geometry:
                propertyEvent(event, provider, UIA_BoundingRectanglePropertyId, owningRuntime);
                propertyEvent(event, provider, UIA_IsOffscreenPropertyId, owningRuntime);
                break;
            case A::PropertyKind::State:
                for(const auto id :
                    {UIA_IsEnabledPropertyId,
                     UIA_IsOffscreenPropertyId,
                     UIA_IsPasswordPropertyId,
                     UIA_ValueIsReadOnlyPropertyId,
                     UIA_RangeValueIsReadOnlyPropertyId,
                     UIA_IsValuePatternAvailablePropertyId,
                     UIA_IsRangeValuePatternAvailablePropertyId,
                     UIA_IsTextPatternAvailablePropertyId,
                     UIA_IsTextPattern2AvailablePropertyId,
                     UIA_IsKeyboardFocusablePropertyId,
                     UIA_HasKeyboardFocusPropertyId,
                     UIA_ToggleToggleStatePropertyId,
                     UIA_ExpandCollapseExpandCollapseStatePropertyId,
                     UIA_SelectionItemIsSelectedPropertyId,
                     UIA_IsRequiredForFormPropertyId,
                     UIA_IsDataValidForFormPropertyId})
                {
                    propertyEvent(event, provider, id, owningRuntime);
                }
                break;
            case A::PropertyKind::Range:
                for(const auto id :
                    {UIA_RangeValueMinimumPropertyId,
                     UIA_RangeValueMaximumPropertyId,
                     UIA_RangeValueSmallChangePropertyId,
                     UIA_RangeValueLargeChangePropertyId})
                {
                    propertyEvent(event, provider, id, owningRuntime);
                }
                break;
            case A::PropertyKind::Exposure:
                static_cast<void>(UiaRaiseStructureChangedEvent(provider, StructureChangeType_ChildrenInvalidated, nullptr, 0));
                break;
            case A::PropertyKind::Metadata:
                for(const auto id :
                    {UIA_HelpTextPropertyId,
                     UIA_CulturePropertyId,
                     UIA_AccessKeyPropertyId,
                     UIA_AcceleratorKeyPropertyId,
                     UIA_IsTextPatternAvailablePropertyId,
                     UIA_IsSelectionPatternAvailablePropertyId,
                     UIA_IsValuePatternAvailablePropertyId,
                     UIA_PositionInSetPropertyId,
                     UIA_SizeOfSetPropertyId,
                     UIA_LevelPropertyId,
                     UIA_GridRowCountPropertyId,
                     UIA_GridColumnCountPropertyId})
                {
                    propertyEvent(event, provider, id, owningRuntime);
                }
                static_cast<void>(UiaRaiseStructureChangedEvent(provider, StructureChangeType_ChildrenInvalidated, nullptr, 0));
                break;
            case A::PropertyKind::Scroll:
                for(const auto id :
                    {UIA_ScrollHorizontalScrollPercentPropertyId,
                     UIA_ScrollVerticalScrollPercentPropertyId,
                     UIA_ScrollHorizontalViewSizePropertyId,
                     UIA_ScrollVerticalViewSizePropertyId,
                     UIA_ScrollHorizontallyScrollablePropertyId,
                     UIA_ScrollVerticallyScrollablePropertyId})
                {
                    propertyEvent(event, provider, id, owningRuntime);
                }
                break;
            case A::PropertyKind::Selection:
            case A::PropertyKind::Text:
                break;
            }
            break;
        }
        }
    }
} // namespace GameWIP::Desktop::Platform
