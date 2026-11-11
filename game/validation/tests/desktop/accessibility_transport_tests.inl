/// @file accessibility_transport_tests.inl
/// @brief Bounded accessibility transport and publication-race tests.

void drain(Desktop::Window &window)
{
    A::Notification notification;

    while(Desktop::TestHooks::popAccessibilityNotification(window, notification))
    {
    }
}

// ------------------------------------------------------------
// Bounded transport and publication races
// ------------------------------------------------------------

void testQueues(Context &context)
{
    Desktop::Window window;

    if(!open(context, window))
    {
        return;
    }

    A::Options options;
    options.limits.maximumActionQueue = 2;
    options.limits.maximumNotificationQueue = 2;
    auto bridge = window.accessibility();
    check(context, "small bounded bridge enables", bridge.enable(options).ok());

    Tree tree;
    check(context, "queue tree publishes", bridge.publish(tree.view()).ok());
    drain(window);

    const auto submit = [&](A::ActionKind kind)
    {
        A::ActionRequest request;
        request.node = 2;
        request.action = kind;
        return Desktop::TestHooks::submitAccessibilityAction(window, std::move(request));
    };

    const auto first = submit(A::ActionKind::Focus), replacement = submit(A::ActionKind::Focus);
    check(
        context,
        "focus tail coalesces",
        first.acceptance == A::ActionAcceptance::Accepted && replacement.requestId > first.requestId && bridge.queueInfo().actions.pending == 1);
    equal(context, "durable Invoke accepted", A::ActionAcceptance::Accepted, submit(A::ActionKind::Invoke).acceptance);
    equal(context, "coalescing cannot cross durable barrier", A::ActionAcceptance::QueueFull, submit(A::ActionKind::Focus).acceptance);
    equal(context, "newest overflow rejected", std::uint64_t{1}, bridge.queueInfo().actions.rejected);

    A::ActionRequest request;
    check(context, "pop coalesced action", bridge.popAction(request));
    equal(context, "latest request ID", replacement.requestId, request.requestId);

    A::Node root;
    root.id = 1;
    const std::array<A::Node, 1> nodes{root};
    check(context, "node removal publishes", bridge.publish({2, 1, nodes}).ok());
    check(context, "already queued action remains deliverable", bridge.popAction(request));
    equal(context, "stale observed generation delivered", A::Generation{1}, request.observedGeneration);
    equal(context, "stale durable kind retained", A::ActionKind::Invoke, request.action);
    check(context, "queue empty", !bridge.popAction(request));

    drain(window);
    check(context, "unchanged full tree restored", bridge.publish(tree.view(3)).ok());
    drain(window);
    check(context, "identical payload newer generation publishes", bridge.publish(tree.view(4)).ok());
    equal(context, "identical payload produces no notifications", std::uint32_t{0}, bridge.queueInfo().notifications.pending);

    tree.nodes[1].name = "Changed";
    tree.nodes[1].description = "Changed too";
    tree.nodes[1].value = "Third change";
    check(context, "pressure still commits snapshot", bridge.publish(tree.view(5)).ok());
    check(context, "pressure collapses to invalidation", bridge.queueInfo().notifications.invalidationPending);
    A::Notification notification;
    check(context, "invalidation pops", Desktop::TestHooks::popAccessibilityNotification(window, notification));
    equal(context, "single invalidation category", A::NotificationKind::TreeInvalidated, notification.kind);
    equal(context, "committed latest generation", A::Generation{5}, notification.generation);

    tree.nodes[1].states.flags |= state(A::State::Focused);
    check(context, "semantic focus publishes", bridge.publish(tree.view(6)).ok());
    drain(window);
    const auto handles = Desktop::Native::Win32::getHandle(window);

    if(handles.status.ok())
    {
        // Deterministic native message boundary, independent of desktop activation policy.
        SendMessageW(handles->window, WM_KILLFOCUS, 0, 0);
        drain(window);
        SendMessageW(handles->window, WM_SETFOCUS, 0, 0);
        bool focusEvent = false;

        while(Desktop::TestHooks::popAccessibilityNotification(window, notification))
        {
            focusEvent = focusEvent || (notification.kind == A::NotificationKind::FocusChanged && notification.node == 2);
        }
        check(context, "host focus regain notifies semantic focused node", focusEvent);
    }

    check(context, "event pump succeeds", Desktop::Events::poll().status.ok());
    check(context, "small bridge closes", window.close().ok());
}

void testCapacityAndInteraction(Context &context)
{
    Desktop::Window window;

    if(!open(context, window))
    {
        return;
    }
    auto bridge = window.accessibility();
    A::Options options;
    options.limits.maximumNodes = 2;
    options.limits.maximumNativeProviders = 2;
    options.limits.maximumNotificationQueue = 3;
    check(context, "bounded provider registry enables", bridge.enable(options).ok());

    Tree tree;
    tree.nodes[0].language = "en-US";
    check(context, "bounded registry initial publication", bridge.publish(tree.view()).ok());
    Com<IRawElementProviderSimple> retained(static_cast<IRawElementProviderSimple *>(Desktop::TestHooks::accessibilityProvider(window, 2)));

    if(!context.expectTrue("registry retains initial provider", static_cast<bool>(retained)))
    {
        return;
    }

    tree.children[0] = 3;
    tree.nodes[1].id = 3;
    equal(context, "new native identity rejects at lifetime bound", Error::SizeLimitExceeded, bridge.publish(tree.view(2)).code);
    equal(context, "provider pressure preserves committed generation", A::Generation{1}, bridge.snapshotInfo().generation);
    check(context, "provider pressure preserves old semantic identity", bridge.readSnapshot().find(2) != nullptr);

    tree.children[0] = 2;
    tree.nodes[1].id = 2;
    check(context, "failed preparation does not consume generation", bridge.publish(tree.view(2)).ok());
    drain(window);

    if(bridge.features().supports(A::Feature::Announcements))
    {
        check(context, "default-locale announcement queues", bridge.announce({1, "Queued speech"}).ok());
        equal(context, "announcement language support is explicit", Error::Unsupported, bridge.announce({1, "Bonjour", "fr-FR"}).code);
        check(context, "announcement matching source language queues", bridge.announce({1, "Queued speech", "en-US"}).ok());
        tree.nodes[0].name = "Updated root";
        tree.nodes[1].name = "Updated button";
        check(context, "notification pressure commits despite announcements", bridge.publish(tree.view(3)).ok());
        check(context, "full notification queue collapses to invalidation", bridge.queueInfo().notifications.invalidationPending);
        drain(window);
        check(
            context,
            "announcement accepted behind invalidation",
            bridge.publish(tree.view(4)).ok() && bridge.announce({1, "Retained speech"}).ok());
        tree.nodes[0].name = "Root again";
        check(context, "diff appended beside announcement", bridge.publish(tree.view(5)).ok());
        A::Notification notification;
        check(context, "queued announcement remains first", Desktop::TestHooks::popAccessibilityNotification(window, notification));
        equal(context, "explicit announcement not silently lost without pressure", A::NotificationKind::LiveRegionChanged, notification.kind);
    }

    Com<IInvokeProvider> invoke;

    if(!pattern(context, retained.value, UIA_InvokePatternId, invoke))
    {
        return;
    }

    check(context, "explicit host interaction disable", window.setUserInteractionEnabled(false).ok());
    equal(context, "disabled host rejects native action", static_cast<HRESULT>(UIA_E_ELEMENTNOTENABLED), invoke->Invoke());
    VARIANT enabled{};
    check(context, "disabled host property query", SUCCEEDED(retained->GetPropertyValue(UIA_IsEnabledPropertyId, &enabled)));
    check(context, "disabled host reports native disabled", enabled.vt == VT_BOOL && enabled.boolVal == VARIANT_FALSE);
    VariantClear(&enabled);
    check(context, "host interaction restore", window.setUserInteractionEnabled(true).ok());

    Desktop::ProgressDialog blocker;
    Desktop::Types::Dialogs::Progress::Description progress;
    progress.owner = &window;
    progress.title = "Accessibility interaction regression";

    if(context.expectTrue("modeless owner blocker opens", blocker.open(progress).ok()))
    {
        equal(context, "modal owner block rejects accessibility action", static_cast<HRESULT>(UIA_E_ELEMENTNOTENABLED), invoke->Invoke());
        check(context, "owner blocker closes", blocker.close().ok());
        static_cast<void>(Desktop::Events::poll());
        equal(context, "unblocked host accepts action", S_OK, invoke->Invoke());
        A::ActionRequest request;
        check(context, "unblocked accepted action drains", bridge.popAction(request));
    }

    check(context, "capacity interaction Window closes", window.close().ok());
}

void testClosePublicationRace(Context &context)
{
    Desktop::Window window;

    if(!open(context, window))
    {
        return;
    }
    auto bridge = window.accessibility();
    check(context, "publication race bridge enables", bridge.enable().ok());
    Tree tree;
    check(context, "publication race initial tree", bridge.publish(tree.view()).ok());

    const auto retained = bridge.readSnapshot();
    const std::string largeText(std::size_t{512} * 1024, 'x');
    tree.nodes[1].text = A::TextContent{largeText};
    std::atomic<bool> started{false};
    Error result = Error::InvalidArgument;
    std::thread publisher(
        [&]
        {
            started = true;
            result = bridge.publish(tree.view(2)).code;
        });

    while(!started.load())
    {
        std::this_thread::yield();
    }

    check(context, "close gates in-flight publication", window.close().ok());
    publisher.join();
    check(context, "concurrent publication either commits before close or observes gate", result == Error::Success || result == Error::NotOpen);
    check(context, "in-flight publication never resurrects active snapshot", !bridge.snapshotInfo().available);
    equal(context, "retained reader survives concurrent close", std::string_view("Run"), retained.find(2)->name);

    if(!open(context, window))
    {
        return;
    }

    check(context, "race Window reenable", bridge.enable().ok());
    tree.nodes[1].text.reset();
    check(context, "fresh activation after race publishes", bridge.publish(tree.view(3)).ok());
    check(context, "race Window final close", window.close().ok());
}
