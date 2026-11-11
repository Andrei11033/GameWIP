/// @file accessibility_geometry_tests.inl
/// @brief Accessibility geometry, cleanup, and concurrency tests.

// ------------------------------------------------------------
// Host geometry, cleanup, and concurrency
// ------------------------------------------------------------

void testGeometryAndExceptionalLifetime(Context &context)
{
    auto window = std::make_unique<Desktop::Window>();

    if(!open(context, *window))
    {
        return;
    }
    auto bridge = window->accessibility();
    check(context, "geometry bridge enables", bridge.enable().ok());

    Tree tree;
    tree.nodes[1].geometry = A::Geometry{{1, 2, 10, 20}, {0, 1, -1, 0, 50, 40}, A::Rect{30, 42, 10, 8}};
    check(context, "transformed geometry publishes", bridge.publish(tree.view()).ok());

    Desktop::TestHooks::PresentationPublicationSnapshot host;
    host.clientSize = {200, 100};
    host.contentScale = {2, 1.5F};
    host.visible = true;
    Desktop::TestHooks::applyPresentationPublicationSnapshot(*window, host);

    const auto origin = window->clientPosition();
    Com<IRawElementProviderSimple> provider(static_cast<IRawElementProviderSimple *>(Desktop::TestHooks::accessibilityProvider(*window, 2)));
    Com<IRawElementProviderSimple> root(static_cast<IRawElementProviderSimple *>(Desktop::TestHooks::accessibilityProvider(*window, 1)));

    if(!context.expectTrue("geometry providers exist", provider && root))
    {
        return;
    }

    Com<IRawElementProviderFragment> fragment;
    check(
        context,
        "geometry fragment",
        SUCCEEDED(provider->QueryInterface(__uuidof(IRawElementProviderFragment), reinterpret_cast<void **>(fragment.put()))));

    if(fragment)
    {
        UiaRect bounds{};
        equal(context, "geometry query", S_OK, fragment->get_BoundingRectangle(&bounds));
        equal(context, "physical clipped x", static_cast<double>(origin.x) + 60, bounds.left);
        equal(context, "physical clipped y", static_cast<double>(origin.y) + 63, bounds.top);
        equal(context, "physical clipped width", 20.0, bounds.width);
        equal(context, "physical clipped height", 12.0, bounds.height);
    }

    Com<IRawElementProviderFragmentRoot> fragmentRoot;
    check(
        context,
        "geometry root",
        SUCCEEDED(root->QueryInterface(__uuidof(IRawElementProviderFragmentRoot), reinterpret_cast<void **>(fragmentRoot.put()))));

    if(fragmentRoot)
    {
        Com<IRawElementProviderFragment> hit;
        equal(context, "inverse affine hit test", S_OK, fragmentRoot->ElementProviderFromPoint(origin.x + 70.0, origin.y + 67.5, hit.put()));
        Com<IRawElementProviderSimple> hitSimple;

        if(hit)
        {
            check(
                context,
                "hit exposes simple identity",
                SUCCEEDED(hit->QueryInterface(__uuidof(IRawElementProviderSimple), reinterpret_cast<void **>(hitSimple.put()))));
        }
        check(context, "hit finds transformed child", hitSimple.value == provider.value);
    }

    check(context, "unexpected native destruction", Desktop::TestHooks::destroyNativeWindow(*window).ok());
    VARIANT value{};
    equal(context, "native destruction immediately gates retained provider", kUnavailable, provider->GetPropertyValue(UIA_NamePropertyId, &value));
    check(context, "native destruction clears active snapshot", !bridge.snapshotInfo().available);
    check(context, "unexpected destruction finalizes", window->close().ok());

    if(!open(context, *window))
    {
        return;
    }

    check(context, "exceptional lifetime reenable", bridge.enable().ok());
    check(context, "exceptional lifetime fresh publish", bridge.publish(tree.view(2)).ok());
    Com<IRawElementProviderSimple> retained(static_cast<IRawElementProviderSimple *>(Desktop::TestHooks::accessibilityProvider(*window, 1)));
    std::thread destroyer(
        [owned = std::move(window)] mutable
        {
            owned.reset();
        });
    destroyer.join();
    equal(context, "wrong-thread destructor gates before native cleanup", kUnavailable, retained->GetPropertyValue(UIA_NamePropertyId, &value));
    check(context, "owner pump drains deferred native cleanup", Desktop::Events::poll().status.ok());
}

void testConcurrentQueries(Context &context)
{
    Desktop::Window window;

    if(!open(context, window))
    {
        return;
    }
    auto bridge = window.accessibility();
    check(context, "concurrent bridge enables", bridge.enable().ok());
    Tree tree;
    check(context, "concurrent initial snapshot", bridge.publish(tree.view()).ok());
    Com<IRawElementProviderSimple> provider(static_cast<IRawElementProviderSimple *>(Desktop::TestHooks::accessibilityProvider(window, 2)));

    if(!context.expectTrue("concurrent provider exists", static_cast<bool>(provider)))
    {
        return;
    }
    std::atomic<bool> failed{false}, stop{false};
    std::thread client(
        [&]
        {
            while(!stop.load())
            {
                VARIANT value{};
                const auto hr = provider->GetPropertyValue(UIA_NamePropertyId, &value);

                if(hr != S_OK && hr != kUnavailable)
                {
                    failed = true;
                }

                if(hr == S_OK && (value.vt != VT_BSTR || SysStringLen(value.bstrVal) == 0))
                {
                    failed = true;
                }
                VariantClear(&value);
            }
        });

    std::thread publisher(
        [&]
        {
            for(A::Generation generation = 2; generation < 100; ++generation)
            {
                tree.nodes[1].name = (generation & 1U) != 0 ? "Odd" : "Even";

                if(!bridge.publish(tree.view(generation)).ok())
                {
                    failed = true;
                }
            }
        });

    publisher.join();
    check(context, "close while native queries active", window.close().ok());
    stop = true;
    client.join();
    check(context, "queries observe complete generations or unavailable", !failed.load());
}
