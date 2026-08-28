/// @file window_child_surface_tests.inl
/// @brief ChildSurface lifecycle, queue, geometry, threading, and native-host validation.

namespace ChildEvents = Window::Types::ChildSurface::Events;

void testChildSurfaces(TestSupport::Context &context)
{
    using ChildEvent = Window::Types::ChildSurface::Event;

    static_cast<void>(
        context.expectTrue("Win32 advertises complete ChildSurface support", Window::supports(Window::Types::Capability::ChildSurface)));

    Window::ChildSurface closed;
    static_cast<void>(context.expectFalse("default ChildSurface is closed", closed.isOpen()));
    static_cast<void>(context.expectEq("default ChildSurface lifetime is Closed", Window::Types::LifetimeState::Closed, closed.lifetimeState()));
    static_cast<void>(context.expectEq("closed ChildSurface has no parent identity", Window::Types::WindowId{}, closed.parentId()));
    static_cast<void>(context.expectEq("closed ChildSurface rect is empty", Window::Types::LogicalRect{}, closed.rect()));
    static_cast<void>(context.expectEq("closed ChildSurface mutation reports NotOpen", ErrorCode::NotOpen, closed.show().code));
    static_cast<void>(context.expectTrue("repeated closed ChildSurface close succeeds", closed.close().ok()));

    Window::Window closedParent;
    static_cast<void>(context.expectEq("closed parent is rejected", ErrorCode::NotOpen, closed.open(closedParent).code));

    Window::Types::Description parentDescription;
    parentDescription.title = "ChildSurface validation parent";
    parentDescription.clientSize = {640, 480};
    parentDescription.visible = false;
    Window::Window parent;
    static_cast<void>(context.expectTrue("ChildSurface parent opens", parent.open(parentDescription, 32).ok()));
    if (!parent.isOpen())
        return;

    std::span<ChildEvent> emptyStorage;
    static_cast<void>(
        context.expectEq("empty caller-owned ChildSurface queue is invalid", ErrorCode::InvalidArgument, closed.open(parent, {}, emptyStorage).code));
    static_cast<void>(context.expectEq("zero ChildSurface queue capacity is invalid", ErrorCode::InvalidArgument, closed.open(parent, {}, 0).code));

    Window::ChildSurface surface;
    static_cast<void>(context.expectTrue("default zero-sized ChildSurface description opens", surface.open(parent).ok()));
    if (!surface.isOpen())
    {
        static_cast<void>(parent.close());
        return;
    }
    const Window::Types::WindowId firstParentId = parent.id();
    static_cast<void>(context.expectEq("ChildSurface retains parent lifetime identity", firstParentId, surface.parentId()));
    static_cast<void>(context.expectTrue("ChildSurface inherits current owner thread", surface.isOwnedByCurrentThread()));
    static_cast<void>(context.expectEq(
        "default ChildSurface queue uses internal storage",
        Window::Types::Events::StorageKind::Internal,
        surface.eventQueueInfo().storage));
    static_cast<void>(context.expectEq(
        "default ChildSurface queue uses the Window subsystem default capacity",
        Window::Events::kDefaultQueueCapacity,
        surface.eventQueueInfo().capacity));
    static_cast<void>(context.expectEq("default ChildSurface size remains zero", Window::Types::LogicalSize{}, surface.size()));
    static_cast<void>(context.expectEq("default ChildSurface pixels remain zero", Window::Types::PixelSize{}, surface.pixelSize()));
    static_cast<void>(context.expectFalse("default ChildSurface is hidden", surface.isVisible()));
    static_cast<void>(context.expectTrue("default ChildSurface enables interaction", surface.isUserInteractionEnabled()));
    static_cast<void>(context.expectEq("second ChildSurface open reports AlreadyOpen", ErrorCode::AlreadyOpen, surface.open(parent).code));

    const auto parentHandle = Window::Native::Win32::getHandle(parent);
    const auto childHandle = Window::Native::Win32::getHandle(surface);
    static_cast<void>(
        context.expectTrue("ChildSurface exposes a borrowed native host", childHandle.status.ok() && childHandle.handle.window != nullptr));
    if (childHandle.status.ok())
    {
        static_cast<void>(context.expectEq("native host is directly parented", parentHandle.handle.window, GetParent(childHandle.handle.window)));
        const DWORD style = static_cast<DWORD>(GetWindowLongPtrW(childHandle.handle.window, GWL_STYLE));
        static_cast<void>(context.expectTrue("native host uses WS_CHILD", (style & WS_CHILD) != 0));
        static_cast<void>(context.expectTrue("native host clips descendants", (style & WS_CLIPCHILDREN) != 0));
        static_cast<void>(context.expectTrue("native host clips siblings", (style & WS_CLIPSIBLINGS) != 0));
    }
    static_cast<void>(context.expectTrue("initial ChildSurface close succeeds", surface.close().ok()));

    std::array<ChildEvent, 4> externalStorage;
    Window::Types::ChildSurface::Description childDescription;
    childDescription.rect = {{-25, 18}, {0, 0}};
    childDescription.visible = true;
    childDescription.userInteractionEnabled = false;
    static_cast<void>(context.expectTrue(
        "ChildSurface reopens with negative position and external storage",
        surface.open(parent, childDescription, externalStorage).ok()));
    static_cast<void>(
        context.expectEq("external queue ownership is reported", Window::Types::Events::StorageKind::External, surface.eventQueueInfo().storage));
    static_cast<void>(context.expectEq("external queue capacity is reported", externalStorage.size(), surface.eventQueueInfo().capacity));
    static_cast<void>(context.expectEq("negative parent-relative position is retained", childDescription.rect.position, surface.position()));
    static_cast<void>(context.expectTrue("requested native visibility is retained while parent is hidden", surface.isVisible()));
    static_cast<void>(context.expectFalse("initial interaction disablement is retained", surface.isUserInteractionEnabled()));

    surface.clearEvents();
    static_cast<void>(context.expectTrue("setPosition accepts negative coordinates", surface.setPosition({-40, -12}).ok()));
    static_cast<void>(context.expectTrue("setSize accepts zero-to-nonzero transition", surface.setSize({160, 90}).ok()));
    static_cast<void>(context.expectTrue("setRect changes position and size together", surface.setRect({{24, 30}, {180, 100}}).ok()));
    static_cast<void>(
        context.expectEq("setRect updates authoritative logical rect", Window::Types::LogicalRect{{24, 30}, {180, 100}}, surface.rect()));
    static_cast<void>(context.expectTrue("physical size follows effective DPI", surface.pixelSize().width >= surface.size().width));
    static_cast<void>(context.expectEq("screenRect extent matches pixel size", surface.pixelSize(), surface.screenRect().size));
    bool foundPositionEvent = false;
    bool foundSizeEvent = false;
    bool foundPixelSizeEvent = false;
    ChildEvent geometryEvent;
    while (surface.popEvent(geometryEvent))
    {
        foundPositionEvent = foundPositionEvent || geometryEvent.getIf<ChildEvents::PositionChanged>() != nullptr;
        foundSizeEvent = foundSizeEvent || geometryEvent.getIf<ChildEvents::SizeChanged>() != nullptr;
        foundPixelSizeEvent = foundPixelSizeEvent || geometryEvent.getIf<ChildEvents::PixelSizeChanged>() != nullptr;
    }
    static_cast<void>(context.expectTrue("ChildSurface geometry queues PositionChanged", foundPositionEvent));
    static_cast<void>(context.expectTrue("ChildSurface geometry queues SizeChanged", foundSizeEvent));
    static_cast<void>(context.expectTrue("ChildSurface geometry queues PixelSizeChanged", foundPixelSizeEvent));
    const Window::Types::LogicalRect authoritativeBeforeParentChange = surface.rect();
    const Window::Types::ScreenPosition screenBeforeParentMove = surface.screenRect().position;
    static_cast<void>(parent.setClientPosition({parent.clientPosition().x + 20, parent.clientPosition().y + 15}));
    static_cast<void>(Window::Events::poll());
    static_cast<void>(context.expectEq("parent movement preserves ChildSurface logical geometry", authoritativeBeforeParentChange, surface.rect()));
    static_cast<void>(
        context.expectTrue("parent movement updates ChildSurface screen geometry", screenBeforeParentMove != surface.screenRect().position));
    static_cast<void>(parent.setClientSize({700, 520}));
    static_cast<void>(Window::Events::poll());
    static_cast<void>(context.expectEq("parent resize performs no ChildSurface layout", authoritativeBeforeParentChange, surface.rect()));

    static_cast<void>(surface.setRect({{-30, -20}, {180, 100}}));
    const auto clippingHandle = Window::Native::Win32::getHandle(surface);
    POINT parentOrigin{};
    RECT childFrame{};
    static_cast<void>(ClientToScreen(parentHandle.handle.window, &parentOrigin));
    static_cast<void>(GetWindowRect(clippingHandle.handle.window, &childFrame));
    static_cast<void>(context.expectTrue(
        "native child may extend beyond parent while native hierarchy clips it",
        childFrame.left < parentOrigin.x && childFrame.top < parentOrigin.y &&
            GetParent(clippingHandle.handle.window) == parentHandle.handle.window));
    static_cast<void>(surface.setRect(authoritativeBeforeParentChange));
    const auto origin = surface.clientToScreen({0, 0});
    static_cast<void>(context.expectTrue("ChildSurface clientToScreen succeeds", origin.status.ok()));
    const auto local = surface.screenToClient(origin.position);
    static_cast<void>(context.expectTrue("ChildSurface screenToClient succeeds", local.status.ok()));
    static_cast<void>(context.expectEq("ChildSurface coordinate conversion round-trips origin", Window::Types::LogicalPosition{}, local.position));

    static_cast<void>(context.expectTrue("ChildSurface show is idempotent", surface.show().ok()));
    static_cast<void>(context.expectTrue("ChildSurface hide succeeds", surface.hide().ok()));
    static_cast<void>(context.expectFalse("hide updates visibility cache", surface.isVisible()));
    static_cast<void>(context.expectTrue("ChildSurface show succeeds", surface.show().ok()));
    static_cast<void>(context.expectTrue("show updates visibility cache", surface.isVisible()));
    bool foundVisibilityEvent = false;
    while (surface.popEvent(geometryEvent))
        foundVisibilityEvent = foundVisibilityEvent || geometryEvent.getIf<ChildEvents::VisibilityChanged>() != nullptr;
    static_cast<void>(context.expectTrue("show and hide queue VisibilityChanged", foundVisibilityEvent));
    static_cast<void>(context.expectTrue("ChildSurface interaction enables", surface.setUserInteractionEnabled(true).ok()));
    static_cast<void>(context.expectTrue("interaction cache updates", surface.isUserInteractionEnabled()));
    static_cast<void>(context.expectTrue("ChildSurface interaction disables", surface.setUserInteractionEnabled(false).ok()));

    Window::ChildSurface sibling;
    static_cast<void>(context.expectTrue("sibling ChildSurface opens", sibling.open(parent, {{{40, 40}, {100, 80}}, true, true}, 8).ok()));
    static_cast<void>(context.expectTrue("bringToFront succeeds", surface.bringToFront().ok()));
    static_cast<void>(context.expectTrue("sendToBack succeeds", surface.sendToBack().ok()));
    static_cast<void>(context.expectTrue("placeAbove succeeds for a valid sibling", surface.placeAbove(sibling).ok()));
    const HWND surfaceHwnd = Window::Native::Win32::getHandle(surface).handle.window;
    const HWND siblingHwnd = Window::Native::Win32::getHandle(sibling).handle.window;
    static_cast<void>(context.expectEq("placeAbove produces adjacent native order", siblingHwnd, GetWindow(surfaceHwnd, GW_HWNDNEXT)));
    static_cast<void>(context.expectTrue("placeBelow succeeds for a valid sibling", surface.placeBelow(sibling).ok()));
    static_cast<void>(context.expectEq("placeBelow produces adjacent native order", surfaceHwnd, GetWindow(siblingHwnd, GW_HWNDNEXT)));
    static_cast<void>(context.expectEq("self sibling is invalid", ErrorCode::InvalidArgument, surface.placeAbove(surface).code));
    Window::ChildSurface closedSibling;
    static_cast<void>(context.expectEq("closed sibling is invalid", ErrorCode::InvalidArgument, surface.placeBelow(closedSibling).code));

    Window::Window otherParent;
    static_cast<void>(otherParent.open(parentDescription, 8));
    Window::ChildSurface otherSurface;
    static_cast<void>(otherSurface.open(otherParent, {}, 8));
    static_cast<void>(context.expectEq("different-parent sibling is invalid", ErrorCode::InvalidArgument, surface.placeAbove(otherSurface).code));
    static_cast<void>(otherSurface.close());
    static_cast<void>(otherParent.close());

    ErrorCode wrongThreadMutation = ErrorCode::Success;
    ErrorCode wrongThreadClose = ErrorCode::Success;
    ErrorCode wrongThreadHandle = ErrorCode::Success;
    bool wrongThreadOwnership = true;
    std::thread wrongThread(
        [&]
        {
            wrongThreadOwnership = surface.isOwnedByCurrentThread();
            wrongThreadMutation = surface.setPosition({1, 2}).code;
            wrongThreadClose = surface.close().code;
            wrongThreadHandle = Window::Native::Win32::getHandle(surface).status.code;
        });
    wrongThread.join();
    static_cast<void>(context.expectFalse("ChildSurface rejects foreign-thread ownership", wrongThreadOwnership));
    static_cast<void>(context.expectEq("wrong-thread mutation reports ResourceBusy", ErrorCode::ResourceBusy, wrongThreadMutation));
    static_cast<void>(context.expectEq("wrong-thread close reports ResourceBusy", ErrorCode::ResourceBusy, wrongThreadClose));
    static_cast<void>(context.expectEq("wrong-thread native handle reports ResourceBusy", ErrorCode::ResourceBusy, wrongThreadHandle));

    surface.clearEvents();
    static_cast<void>(surface.setPosition({10, 10}));
    static_cast<void>(surface.setPosition({20, 20}));
    static_cast<void>(context.expectEq("consecutive position events coalesce", std::size_t{1}, surface.eventQueueInfo().pendingEvents));
    ChildEvent event;
    static_cast<void>(context.expectTrue("coalesced ChildSurface event pops", surface.popEvent(event)));
    const auto *position = event.getIf<ChildEvents::PositionChanged>();
    static_cast<void>(context.expectTrue("coalesced event has PositionChanged payload", position != nullptr));
    if (position != nullptr)
        static_cast<void>(context.expectEq("coalesced position retains latest value", Window::Types::LogicalPosition{20, 20}, position->position));
    const std::uint64_t lifetimeSequence = event.sequence;

    static_cast<void>(sibling.close());
    static_cast<void>(surface.close());
    static_cast<void>(context.expectTrue(
        "explicit close releases caller-owned ChildSurface event slots",
        std::ranges::all_of(
            externalStorage,
            [](const ChildEvent &stored)
            {
                return stored.sequence == 0;
            })));
    static_cast<void>(context.expectTrue("ChildSurface reopens after explicit finalization", surface.open(parent, {}, 2).ok()));
    static_cast<void>(surface.setPosition({1, 1}));
    static_cast<void>(surface.popEvent(event));
    static_cast<void>(context.expectEq("event sequence restarts for each lifetime", std::uint64_t{1}, event.sequence));
    static_cast<void>(context.expectTrue("prior lifetime produced a normal sequence", lifetimeSequence >= 1));

#if WINDOW_INTERNAL_TEST_HOOKS
    const Window::Types::LogicalRect dpiRect{{-15, 12}, {800, 600}};
    const auto dpi96 = Window::TestHooks::calculateChildSurfaceDpiTransition(dpiRect, 96);
    const auto dpi144 = Window::TestHooks::calculateChildSurfaceDpiTransition(dpi96.logicalRect, 144);
    const auto dpi144Again = Window::TestHooks::calculateChildSurfaceDpiTransition(dpi144.logicalRect, 144);
    static_cast<void>(context.expectEq("ChildSurface DPI transition preserves logical rect", dpiRect, dpi144.logicalRect));
    static_cast<void>(context.expectEq("ChildSurface DPI transition scales physical pixels", Window::Types::PixelSize{1200, 900}, dpi144.pixelSize));
    static_cast<void>(context.expectEq("ChildSurface DPI conversion has no cumulative drift", dpi144.pixelSize, dpi144Again.pixelSize));

    surface.clearEvents();
    static_cast<void>(surface.setPosition({2, 2}));
    static_cast<void>(surface.show());
    static_cast<void>(context.expectEq("two-slot queue fills", std::size_t{2}, surface.eventQueueInfo().pendingEvents));
    static_cast<void>(context.expectTrue("unexpected native host destruction succeeds", Window::TestHooks::destroyNativeChildSurface(surface).ok()));
    static_cast<void>(context.expectFalse("unexpected native destruction closes native access", surface.isOpen()));
    static_cast<void>(context.expectEq(
        "unexpected native destruction retains pending-finalize state",
        Window::Types::LifetimeState::NativeDestroyedPendingFinalize,
        surface.lifetimeState()));
    static_cast<void>(
        context.expectEq("native handle invalidates after destruction", ErrorCode::NotOpen, Window::Native::Win32::getHandle(surface).status.code));
    static_cast<void>(context.expectEq("reopen before finalization is rejected", ErrorCode::AlreadyOpen, surface.open(parent).code));
    bool foundNativeDestroyed = false;
    while (surface.popEvent(event))
        foundNativeDestroyed = foundNativeDestroyed || event.getIf<ChildEvents::NativeDestroyed>() != nullptr;
    static_cast<void>(context.expectTrue("NativeDestroyed survives a full queue", foundNativeDestroyed));
    static_cast<void>(context.expectTrue("NativeDestroyed eviction increments drop count", surface.eventQueueInfo().droppedEvents > 0));
    surface.clearDroppedEventCount();
    static_cast<void>(context.expectEq("ChildSurface drop counter clears", std::uint64_t{0}, surface.eventQueueInfo().droppedEvents));
    static_cast<void>(context.expectTrue("pending native destruction finalizes", surface.close().ok()));

    Window::ChildSurface pumpedSurface;
    static_cast<void>(pumpedSurface.open(parent, {}, 4));
    const HWND pumpedHandle = Window::Native::Win32::getHandle(pumpedSurface).handle.window;
    static_cast<void>(PostMessageW(pumpedHandle, WM_CLOSE, 0, 0));
    const Window::Types::Events::PumpResult childPump = Window::Events::poll();
    static_cast<void>(context.expectTrue("shared event pump processes ChildSurface native messages", childPump.status.ok()));
    static_cast<void>(context.expectEq("shared pump counts a ChildSurface event", std::size_t{1}, childPump.eventsQueued));
    static_cast<void>(context.expectEq("shared pump reports no ChildSurface drop", std::uint64_t{0}, childPump.eventsDropped));
    static_cast<void>(context.expectEq(
        "pumped native destruction enters pending finalization",
        Window::Types::LifetimeState::NativeDestroyedPendingFinalize,
        pumpedSurface.lifetimeState()));
    static_cast<void>(pumpedSurface.close());

    Window::TestHooks::failNext(Window::TestHooks::FailurePoint::Allocation);
    static_cast<void>(context.expectEq("ChildSurface allocation failure is translated", ErrorCode::OutOfMemory, surface.open(parent, {}, 4).code));
    Window::TestHooks::failNext(Window::TestHooks::FailurePoint::NativeCreation);
    static_cast<void>(context.expectEq("ChildSurface native creation failure rolls back", ErrorCode::OpenFailed, surface.open(parent, {}, 4).code));
    static_cast<void>(context.expectFalse("failed ChildSurface open leaves no lifetime", surface.isOpen()));
    static_cast<void>(context.expectTrue("ChildSurface recovers after open rollback", surface.open(parent, {}, 4).ok()));
    Window::TestHooks::failNext(Window::TestHooks::FailurePoint::Close);
    static_cast<void>(context.expectEq("ChildSurface close failure is retryable", ErrorCode::CloseFailed, surface.close().code));
    static_cast<void>(context.expectTrue("failed ChildSurface close preserves native lifetime", surface.isOpen()));
    static_cast<void>(context.expectTrue("ChildSurface close retry succeeds", surface.close().ok()));
#endif

    Window::ChildSurface parentLoss;
    static_cast<void>(parentLoss.open(parent, {{{-12, -8}, {80, 60}}, true, true}, 4));
    const Window::Types::WindowId retainedParentId = parent.id();
    static_cast<void>(context.expectTrue("parent closes before ChildSurface object", parent.close().ok()));
    static_cast<void>(context.expectFalse("parent destruction invalidates child host", parentLoss.isOpen()));
    static_cast<void>(context.expectEq(
        "parent destruction leaves ChildSurface pending finalization",
        Window::Types::LifetimeState::NativeDestroyedPendingFinalize,
        parentLoss.lifetimeState()));
    static_cast<void>(context.expectEq("parent identity remains cached after parent destruction", retainedParentId, parentLoss.parentId()));
    static_cast<void>(context.expectTrue("parent-loss ChildSurface finalizes", parentLoss.close().ok()));

    static_cast<void>(parent.open(parentDescription, 8));
    auto abandoned = std::make_unique<Window::ChildSurface>();
    static_cast<void>(abandoned->open(parent, {{{0, 0}, {32, 24}}, false, true}, 4));
    const HWND abandonedHandle = Window::Native::Win32::getHandle(*abandoned).handle.window;
    std::thread destroyer(
        [ownedSurface = std::move(abandoned)]() mutable
        {
            ownedSurface.reset();
        });
    destroyer.join();
    static_cast<void>(context.expectTrue("wrong-thread destructor transfers cleanup until owner pump", IsWindow(abandonedHandle) != FALSE));
    static_cast<void>(Window::Events::poll());
    static_cast<void>(context.expectFalse("owner dispatcher completes deferred ChildSurface cleanup", IsWindow(abandonedHandle) != FALSE));
    static_cast<void>(parent.close());
}

/// @brief Visually validates a real Win32 descendant hosted below a ChildSurface HWND.
void testManualChildSurface(TestSupport::Context &context, const GameWIP::Test::WindowTestOptions &options)
{
    if (!beginManualSuite(context, options, "Window native child surface"))
        return;

    Window::Window parent;
    if (!openManualWindow(context, parent, "GameWIP ChildSurface native-host validation"))
        return;
    Window::ChildSurface surface;
    Window::Types::ChildSurface::Description description;
    description.rect = {{180, 140}, {600, 260}};
    description.visible = true;
    if (!requireManualStatus(context, "manual ChildSurface opens", surface.open(parent, description, 32)))
    {
        static_cast<void>(parent.close());
        return;
    }

    const auto native = Window::Native::Win32::getHandle(surface);
    HWND button = nullptr;
    if (native.status.ok())
    {
        button = CreateWindowExW(
            0,
            L"BUTTON",
            L"Real Win32 descendant hosted by GameWIP ChildSurface",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            60,
            80,
            480,
            80,
            native.handle.window,
            nullptr,
            native.handle.instance,
            nullptr);
    }
    static_cast<void>(context.expectTrue("manual native descendant is created", button != nullptr));
    if (button != nullptr)
    {
        recordManualCheck(
            context,
            parent,
            "native descendant embedding",
            "Is a native button labeled 'Real Win32 descendant hosted by GameWIP ChildSurface' visible inside the validation Window?");
        static_cast<void>(DestroyWindow(button));
    }
    static_cast<void>(surface.close());
    static_cast<void>(parent.close());
}
