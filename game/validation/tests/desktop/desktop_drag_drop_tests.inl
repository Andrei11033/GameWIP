/// @file desktop_drag_drop_tests.inl
/// @brief Portable DragDrop policy, lifecycle, region, and queue validation.

void testDragDrop(TestSupport::Context &context)
{
    namespace DD = Desktop::Types::DragDrop;
    namespace DDEvents = DD::Events;
    namespace Transfer = Desktop::Types::DataTransfer;

    static_assert(!std::is_copy_constructible_v<Desktop::DragDropTarget>);
    static_assert(!std::is_move_constructible_v<Desktop::DragDropTarget>);
    static_assert(noexcept(std::declval<Desktop::DragDropTarget &>().close()));
    static_assert(noexcept(Desktop::DragDrop::beginDrag(std::declval<Desktop::Window &>(), DD::Description{})));
    static_cast<void>(context.expectFalse("zero session identity is invalid", DD::SessionId{}.isValid()));
    static_cast<void>(context.expectFalse("zero region identity is invalid", DD::RegionId{}.isValid()));
    static_cast<void>(context.expectTrue("nonzero session identity is valid", DD::SessionId{1}.isValid()));
    static_cast<void>(context.expectTrue("nonzero region identity is valid", DD::RegionId{1}.isValid()));

    constexpr DD::Effect all = DD::Effect::Copy | DD::Effect::Move | DD::Effect::Link;
    static_cast<void>(context.expectEq(
        "no effect intersection rejects",
        DD::Effect::None,
        Desktop::TestHooks::negotiateDragDropEffect(DD::Effect::Copy, DD::Effect::Move, DD::Effect::Move)));
    static_cast<void>(context.expectEq(
        "single effect intersection wins",
        DD::Effect::Move,
        Desktop::TestHooks::negotiateDragDropEffect(all, DD::Effect::Move, DD::Effect::Move)));
    static_cast<void>(
        context.expectEq("preferred candidate wins", DD::Effect::Link, Desktop::TestHooks::negotiateDragDropEffect(all, all, DD::Effect::Link)));
    static_cast<void>(
        context.expectEq("fallback prefers Copy", DD::Effect::Copy, Desktop::TestHooks::negotiateDragDropEffect(all, all, DD::Effect::None)));
    static_cast<void>(context.expectEq(
        "fallback prefers Move when Copy is absent",
        DD::Effect::Move,
        Desktop::TestHooks::negotiateDragDropEffect(DD::Effect::Move | DD::Effect::Link, all, DD::Effect::None)));
    static_cast<void>(context.expectEq(
        "fallback selects Link when it is the only candidate",
        DD::Effect::Link,
        Desktop::TestHooks::negotiateDragDropEffect(DD::Effect::Link, all, DD::Effect::None)));
    static_cast<void>(context.expectEq(
        "invalid effect bits normalize to None",
        DD::Effect::None,
        Desktop::TestHooks::negotiateDragDropEffect(static_cast<DD::Effect>(0x80), all, DD::Effect::Copy)));

    const DD::Result rejectedDrop = Desktop::TestHooks::droppedDragDropSourceResult(DD::Effect::None, DD::Effect::Copy);
    static_cast<void>(context.expectTrue("rejected native drop completes successfully", rejectedDrop.status.ok()));
    static_cast<void>(context.expectEq("rejected native drop is cancelled", DD::Outcome::Cancelled, rejectedDrop.outcome));
    static_cast<void>(context.expectEq("rejected native drop performs no effect", DD::Effect::None, rejectedDrop.effect));
    const DD::Result acceptedDrop = Desktop::TestHooks::droppedDragDropSourceResult(DD::Effect::Move, all);
    static_cast<void>(context.expectTrue("accepted native drop completes successfully", acceptedDrop.status.ok()));
    static_cast<void>(context.expectEq("accepted native drop is dropped", DD::Outcome::Dropped, acceptedDrop.outcome));
    static_cast<void>(context.expectEq("accepted native drop reports its effect", DD::Effect::Move, acceptedDrop.effect));
    static_cast<void>(context.expectEq(
        "multiple native completion effects fail",
        ErrorCode::NativeFailure,
        Desktop::TestHooks::droppedDragDropSourceResult(DD::Effect::Copy | DD::Effect::Move, all).status.code));
    static_cast<void>(context.expectEq(
        "unadvertised native completion effect fails",
        ErrorCode::NativeFailure,
        Desktop::TestHooks::droppedDragDropSourceResult(DD::Effect::Move, DD::Effect::Copy).status.code));

    std::array<Transfer::ItemView, 1> validTextItems{{Transfer::TextView{"text"}}};
    DD::Description validSource{validTextItems, DD::Effect::Copy, DD::TriggerButton::Left};
    static_cast<void>(context.expectTrue("valid source data prepares", Desktop::TestHooks::prepareDragDropSource(validSource).ok()));
    static_cast<void>(context.expectEq("empty source is invalid", ErrorCode::InvalidArgument, Desktop::TestHooks::prepareDragDropSource({}).code));
    DD::Description invalidSourceEffects = validSource;
    invalidSourceEffects.allowedEffects = static_cast<DD::Effect>(0x80);
    static_cast<void>(context.expectEq(
        "invalid source effect bits are rejected",
        ErrorCode::InvalidArgument,
        Desktop::TestHooks::prepareDragDropSource(invalidSourceEffects).code));
    DD::Description invalidTrigger = validSource;
    invalidTrigger.triggerButton = static_cast<DD::TriggerButton>(99);
    static_cast<void>(context.expectEq(
        "invalid source trigger is rejected",
        ErrorCode::InvalidArgument,
        Desktop::TestHooks::prepareDragDropSource(invalidTrigger).code));
    std::array<Transfer::ItemView, 2> duplicateTextItems{{Transfer::TextView{"one"}, Transfer::TextView{"two"}}};
    static_cast<void>(context.expectEq(
        "duplicate native source formats are rejected",
        ErrorCode::InvalidArgument,
        Desktop::TestHooks::prepareDragDropSource({duplicateTextItems, DD::Effect::Copy, DD::TriggerButton::Left}).code));
    std::array<Transfer::ItemView, 1> malformedTextItems{{Transfer::TextView{std::string_view{"\xC3", 1}}}};
    static_cast<void>(context.expectEq(
        "malformed source UTF-8 is rejected",
        ErrorCode::InvalidArgument,
        Desktop::TestHooks::prepareDragDropSource({malformedTextItems, DD::Effect::Copy, DD::TriggerButton::Left}).code));
    std::array<Transfer::ItemView, 1> nulTextItems{{Transfer::TextView{std::string_view{"a\0b", 3}}}};
    static_cast<void>(context.expectEq(
        "source text embedded NUL is rejected",
        ErrorCode::InvalidArgument,
        Desktop::TestHooks::prepareDragDropSource({nulTextItems, DD::Effect::Copy, DD::TriggerButton::Left}).code));
    std::array<std::byte, 1> oneByte{std::byte{1}};
    std::array<Transfer::ItemView, 1> malformedCustomItems{{Transfer::CustomView{std::string_view{"\xC3", 1}, oneByte}}};
    static_cast<void>(context.expectEq(
        "malformed source custom name is rejected",
        ErrorCode::InvalidArgument,
        Desktop::TestHooks::prepareDragDropSource({malformedCustomItems, DD::Effect::Copy, DD::TriggerButton::Left}).code));
    std::array<Transfer::ItemView, 1> nulCustomItems{{Transfer::CustomView{std::string_view{"a\0b", 3}, oneByte}}};
    static_cast<void>(context.expectEq(
        "source custom name embedded NUL is rejected",
        ErrorCode::InvalidArgument,
        Desktop::TestHooks::prepareDragDropSource({nulCustomItems, DD::Effect::Copy, DD::TriggerButton::Left}).code));
    std::array<Transfer::ItemView, 1> emptyCustomItems{{Transfer::CustomView{"GameWIP.Empty", {}}}};
    static_cast<void>(context.expectEq(
        "zero-byte custom source is unsupported",
        ErrorCode::Unsupported,
        Desktop::TestHooks::prepareDragDropSource({emptyCustomItems, DD::Effect::Copy, DD::TriggerButton::Left}).code));
    std::array<std::byte, 3> invalidImageBytes{};
    std::array<Transfer::ItemView, 1> invalidImageItems{{Transfer::ImageView{{1, 1}, 0, invalidImageBytes}}};
    static_cast<void>(context.expectEq(
        "invalid image extent is rejected",
        ErrorCode::InvalidArgument,
        Desktop::TestHooks::prepareDragDropSource({invalidImageItems, DD::Effect::Copy, DD::TriggerButton::Left}).code));
    std::array<Transfer::ItemView, 1> zeroImageItems{{Transfer::ImageView{{0, 1}, 0, {}}}};
    static_cast<void>(context.expectEq(
        "zero image dimension is rejected",
        ErrorCode::InvalidArgument,
        Desktop::TestHooks::prepareDragDropSource({zeroImageItems, DD::Effect::Copy, DD::TriggerButton::Left}).code));
    std::array<std::byte, 4> onePixel{};
    std::array<Transfer::ItemView, 1> invalidStrideItems{{Transfer::ImageView{{1, 1}, 3, onePixel}}};
    static_cast<void>(context.expectEq(
        "undersized image stride is rejected",
        ErrorCode::InvalidArgument,
        Desktop::TestHooks::prepareDragDropSource({invalidStrideItems, DD::Effect::Copy, DD::TriggerButton::Left}).code));
    std::array<GameWIP::FileSystem::Types::Path, 1> relativePaths{{L"relative.txt"}};
    std::array<Transfer::ItemView, 1> relativeFileItems{{Transfer::FileListView{relativePaths}}};
    static_cast<void>(context.expectEq(
        "relative source file path is rejected",
        ErrorCode::InvalidArgument,
        Desktop::TestHooks::prepareDragDropSource({relativeFileItems, DD::Effect::Copy, DD::TriggerButton::Left}).code));
    std::wstring malformedNativePath = L"C:\\";
    malformedNativePath.push_back(static_cast<wchar_t>(0xD800));
    std::array<GameWIP::FileSystem::Types::Path, 1> malformedPaths{{malformedNativePath}};
    std::array<Transfer::ItemView, 1> malformedFileItems{{Transfer::FileListView{malformedPaths}}};
    static_cast<void>(context.expectEq(
        "malformed native Unicode source path is rejected",
        ErrorCode::InvalidArgument,
        Desktop::TestHooks::prepareDragDropSource({malformedFileItems, DD::Effect::Copy, DD::TriggerButton::Left}).code));
    Desktop::TestHooks::failNext(Desktop::TestHooks::FailurePoint::DragDropPreparation);
    static_cast<void>(context.expectEq(
        "source preparation failure is deterministic",
        ErrorCode::OutOfMemory,
        Desktop::TestHooks::prepareDragDropSource(validSource).code));
    Desktop::TestHooks::failNext(Desktop::TestHooks::FailurePoint::DragDropOleInitialization);
    static_cast<void>(context.expectEq(
        "source OLE initialization failure is deterministic",
        ErrorCode::OpenFailed,
        Desktop::TestHooks::testDragDropOleInitialization().code));
    ErrorCode compatiblePreinitialized = ErrorCode::Unknown;
    ErrorCode incompatibleApartment = ErrorCode::Unknown;
    std::thread compatibleApartmentThread(
        [&]
        {
            const HRESULT initialized = OleInitialize(nullptr);
            if (initialized == S_OK || initialized == S_FALSE)
                compatiblePreinitialized = Desktop::TestHooks::testDragDropOleInitialization().code;
            if (initialized == S_OK || initialized == S_FALSE)
                OleUninitialize();
        });
    compatibleApartmentThread.join();
    static_cast<void>(context.expectEq("compatible preinitialized OLE apartment is reused", ErrorCode::Success, compatiblePreinitialized));
    std::thread incompatibleApartmentThread(
        [&]
        {
            const HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            if (initialized == S_OK || initialized == S_FALSE)
                incompatibleApartment = Desktop::TestHooks::testDragDropOleInitialization().code;
            if (initialized == S_OK || initialized == S_FALSE)
                CoUninitialize();
        });
    incompatibleApartmentThread.join();
    static_cast<void>(context.expectEq("incompatible OLE apartment is ResourceBusy", ErrorCode::ResourceBusy, incompatibleApartment));
    Desktop::TestHooks::failNext(Desktop::TestHooks::FailurePoint::DragDropMaterialization);
    static_cast<void>(context.expectEq(
        "target materialization failure is deterministic",
        ErrorCode::ReadFailed,
        Desktop::TestHooks::testDragDropMaterialization().code));
    static_cast<void>(context.expectTrue("source COM data-object and enumerator contracts hold", Desktop::TestHooks::dragDropComContractsValid()));

    Desktop::DragDropTarget target;
    static_cast<void>(context.expectFalse("default target is closed", target.isOpen()));
    static_cast<void>(context.expectEq("default target lifetime is Closed", Desktop::Types::LifetimeState::Closed, target.lifetimeState()));
    static_cast<void>(context.expectEq("closed target has invalid Window identity", Desktop::Types::WindowId{}, target.windowId()));
    static_cast<void>(context.expectTrue("repeated closed target close succeeds", target.close().ok()));
    Desktop::Window closedWindow;
    static_cast<void>(context.expectEq("closed Window target open is rejected", ErrorCode::NotOpen, target.open(closedWindow, {}).code));
    static_cast<void>(
        context.expectEq("closed source drag is rejected", ErrorCode::NotOpen, Desktop::DragDrop::beginDrag(closedWindow, {}).status.code));

    Desktop::Types::Description windowDescription;
    windowDescription.title = "DragDrop validation target";
    windowDescription.clientSize = {320, 200};
    windowDescription.visible = false;
    Desktop::Window window;
    if (!context.expectTrue("DragDrop Window opens", window.open(windowDescription, 8).ok()))
        return;

    std::array<Transfer::FormatView, 1> textFormat{{{Transfer::FormatKind::Text, {}}}};
    static_cast<void>(
        context.expectEq("empty target region list is rejected", ErrorCode::InvalidArgument, target.open(window, DD::TargetDescription{}).code));
    std::array<DD::RegionDescription, 1> emptyFormatRegion{{{DD::RegionId{1}, std::nullopt, {}, DD::Effect::Copy, DD::Effect::Copy}}};
    static_cast<void>(context.expectEq(
        "empty accepted format list is rejected",
        ErrorCode::InvalidArgument,
        target.open(window, DD::TargetDescription{emptyFormatRegion}).code));
    std::array<DD::RegionDescription, 2> duplicateRegions{
        {{DD::RegionId{1}, std::nullopt, textFormat, DD::Effect::Copy, DD::Effect::Copy},
         {DD::RegionId{1}, Desktop::Types::LogicalRect{{0, 0}, {10, 10}}, textFormat, DD::Effect::Copy, DD::Effect::Copy}}};
    static_cast<void>(context.expectEq(
        "duplicate region IDs are rejected",
        ErrorCode::InvalidArgument,
        target.open(window, DD::TargetDescription{duplicateRegions}).code));
    std::array<DD::RegionDescription, 1> invalidPreferred{{{DD::RegionId{1}, std::nullopt, textFormat, DD::Effect::Copy, DD::Effect::Move}}};
    static_cast<void>(context.expectEq(
        "preferred effect must be allowed",
        ErrorCode::InvalidArgument,
        target.open(window, DD::TargetDescription{invalidPreferred}).code));
    std::array<Transfer::FormatView, 1> malformedFormat{{{Transfer::FormatKind::Custom, std::string_view{"\xC3", 1}}}};
    std::array<DD::RegionDescription, 1> malformedRegion{{{DD::RegionId{1}, std::nullopt, malformedFormat, DD::Effect::Copy, DD::Effect::Copy}}};
    static_cast<void>(context.expectEq(
        "malformed custom format name is rejected",
        ErrorCode::InvalidArgument,
        target.open(window, DD::TargetDescription{malformedRegion}).code));
    std::array<Transfer::FormatView, 1> nulFormat{{{Transfer::FormatKind::Custom, std::string_view{"a\0b", 3}}}};
    std::array<DD::RegionDescription, 1> nulFormatRegion{{{DD::RegionId{1}, std::nullopt, nulFormat, DD::Effect::Copy, DD::Effect::Copy}}};
    static_cast<void>(context.expectEq(
        "custom format embedded NUL is rejected",
        ErrorCode::InvalidArgument,
        target.open(window, DD::TargetDescription{nulFormatRegion}).code));
    std::array<Transfer::FormatView, 2> duplicateStandard{{{Transfer::FormatKind::Text, {}}, {Transfer::FormatKind::Text, {}}}};
    std::array<DD::RegionDescription, 1> duplicateStandardRegion{
        {{DD::RegionId{1}, std::nullopt, duplicateStandard, DD::Effect::Copy, DD::Effect::Copy}}};
    static_cast<void>(context.expectEq(
        "duplicate standard target formats are rejected",
        ErrorCode::InvalidArgument,
        target.open(window, DD::TargetDescription{duplicateStandardRegion}).code));
    std::array<Transfer::FormatView, 2> duplicateCustom{
        {{Transfer::FormatKind::Custom, "GameWIP.Same"}, {Transfer::FormatKind::Custom, "GameWIP.Same"}}};
    std::array<DD::RegionDescription, 1> duplicateCustomRegion{
        {{DD::RegionId{1}, std::nullopt, duplicateCustom, DD::Effect::Copy, DD::Effect::Copy}}};
    static_cast<void>(context.expectEq(
        "duplicate custom target formats are rejected",
        ErrorCode::InvalidArgument,
        target.open(window, DD::TargetDescription{duplicateCustomRegion}).code));
    std::array<Transfer::FormatView, 2> duplicateNativeCustom{
        {{Transfer::FormatKind::Custom, "GameWIP.CaseIdentity"}, {Transfer::FormatKind::Custom, "gamewip.caseidentity"}}};
    std::array<DD::RegionDescription, 1> duplicateNativeCustomRegion{
        {{DD::RegionId{1}, std::nullopt, duplicateNativeCustom, DD::Effect::Copy, DD::Effect::Copy}}};
    static_cast<void>(context.expectEq(
        "duplicate native custom identity is rejected",
        ErrorCode::InvalidArgument,
        target.open(window, DD::TargetDescription{duplicateNativeCustomRegion}).code));
    std::array<DD::RegionDescription, 1> invalidEffectRegion{
        {{DD::RegionId{1}, std::nullopt, textFormat, static_cast<DD::Effect>(0x80), DD::Effect::Copy}}};
    static_cast<void>(context.expectEq(
        "invalid target effect bits are rejected",
        ErrorCode::InvalidArgument,
        target.open(window, DD::TargetDescription{invalidEffectRegion}).code));
    std::array<DD::RegionDescription, 1> combinedPreferredRegion{
        {{DD::RegionId{1}, std::nullopt, textFormat, all, DD::Effect::Copy | DD::Effect::Move}}};
    static_cast<void>(context.expectEq(
        "combined preferred target effect is rejected",
        ErrorCode::InvalidArgument,
        target.open(window, DD::TargetDescription{combinedPreferredRegion}).code));
    std::array<DD::RegionDescription, 1> overflowingRectRegion{
        {{DD::RegionId{1},
          Desktop::Types::LogicalRect{{std::numeric_limits<std::int32_t>::max(), 0}, {1, 1}},
          textFormat,
          DD::Effect::Copy,
          DD::Effect::Copy}}};
    static_cast<void>(context.expectEq(
        "overflowing target rectangle is rejected",
        ErrorCode::InvalidArgument,
        target.open(window, DD::TargetDescription{overflowingRectRegion}).code));
    std::array<DD::RegionDescription, 1> emptyRectRegion{
        {{DD::RegionId{1}, Desktop::Types::LogicalRect{{0, 0}, {0, 1}}, textFormat, DD::Effect::Copy, DD::Effect::Copy}}};
    static_cast<void>(context.expectEq(
        "empty target rectangle is rejected",
        ErrorCode::InvalidArgument,
        target.open(window, DD::TargetDescription{emptyRectRegion}).code));
    static_cast<void>(context.expectEq("zero event capacity is invalid", ErrorCode::InvalidArgument, target.open(window, {}, 0).code));

    std::array<DD::RegionDescription, 2> regions{
        {{DD::RegionId{1}, std::nullopt, textFormat, all, DD::Effect::Move},
         {DD::RegionId{2}, Desktop::Types::LogicalRect{{20, 20}, {80, 60}}, textFormat, DD::Effect::Copy, DD::Effect::Copy}}};
    static_cast<void>(context.expectEq(
        "impossible internal queue capacity is invalid",
        ErrorCode::InvalidArgument,
        target.open(window, DD::TargetDescription{regions}, std::numeric_limits<std::size_t>::max()).code));
    Desktop::TestHooks::failNext(Desktop::TestHooks::FailurePoint::DragDropOleInitialization);
    static_cast<void>(context.expectEq(
        "target OLE initialization failure is deterministic",
        ErrorCode::OpenFailed,
        target.open(window, DD::TargetDescription{regions}, 3).code));
    Desktop::TestHooks::failNext(Desktop::TestHooks::FailurePoint::DragDropRegistration);
    static_cast<void>(context.expectEq(
        "native registration failure rolls back",
        ErrorCode::OpenFailed,
        target.open(window, DD::TargetDescription{regions}, 3).code));
    static_cast<void>(context.expectFalse("failed registration leaves target closed", target.isOpen()));
    static_cast<void>(
        context.expectEq("failed registration removes its active registry entry", std::size_t{0}, Desktop::TestHooks::activeDragDropTargetCount()));
    static_cast<void>(context.expectTrue("failed registration leaves the Window usable", window.isOpen()));
    static_cast<void>(context.expectFalse("failed registration preserves lightweight mode", window.fileDropEnabled()));
    const auto open = target.open(window, DD::TargetDescription{regions}, 3);
    if (!open.ok())
    {
        context.skip("native OLE target lifecycle", "the test thread has an incompatible OLE apartment");
        static_cast<void>(window.close());
        return;
    }
    static_cast<void>(context.expectTrue("target opens lazily", target.isOpen()));
    static_cast<void>(context.expectEq("target retains Window identity", window.id(), target.windowId()));
    static_cast<void>(context.expectTrue("target inherits owner thread", target.ownedByCurrentThread()));
    static_cast<void>(context.expectEq("open copies every supplied region", std::size_t{2}, Desktop::TestHooks::dragDropRegionCount(target)));
    static_cast<void>(context.expectEq(
        "whole-client region matches outside rectangles",
        DD::RegionId{1},
        Desktop::TestHooks::matchDragDropRegion(target, {5, 5}, textFormat)));
    static_cast<void>(
        context.expectEq("last overlapping region wins", DD::RegionId{2}, Desktop::TestHooks::matchDragDropRegion(target, {30, 30}, textFormat)));
    regions[1].id = DD::RegionId{77};
    regions[1].rect = Desktop::Types::LogicalRect{{200, 100}, {10, 10}};
    static_cast<void>(context.expectEq(
        "open publishes a copy independent of caller region storage",
        DD::RegionId{2},
        Desktop::TestHooks::matchDragDropRegion(target, {30, 30}, textFormat)));
    regions[1] = {DD::RegionId{2}, Desktop::Types::LogicalRect{{20, 20}, {80, 60}}, textFormat, DD::Effect::Copy, DD::Effect::Copy};
    std::array<Transfer::FormatView, 1> filesOnly{{{Transfer::FormatKind::FileList, {}}}};
    static_cast<void>(context.expectEq(
        "unaccepted metadata produces no region",
        DD::RegionId{},
        Desktop::TestHooks::matchDragDropRegion(target, {30, 30}, filesOnly)));
    static_cast<void>(context.expectTrue(
        "session allocation skips wrapped zero",
        Desktop::TestHooks::nextDragDropSessionId(target, std::numeric_limits<std::uint64_t>::max()).isValid()));
    static_cast<void>(
        context.expectTrue("session allocation remains nonzero after wrap", Desktop::TestHooks::nextDragDropSessionId(target, 0).isValid()));
    static_cast<void>(
        context.expectEq("target reports internal queue", Desktop::Types::Events::StorageKind::Internal, target.eventQueueInfo().storage));
    static_cast<void>(context.expectEq("second full target is busy", ErrorCode::ResourceBusy, Desktop::DragDropTarget{}.open(window, {}).code));
    static_cast<void>(context.expectEq("lightweight mode cannot replace full target", ErrorCode::ResourceBusy, window.setFileDropEnabled(true).code));

    const auto formats = std::make_shared<const std::vector<Transfer::Format>>(std::vector<Transfer::Format>{{Transfer::FormatKind::Text, {}}});
    static_cast<void>(Desktop::TestHooks::enqueueDragDrop(target, DDEvents::Entered{{1}, {}, {1}, DD::Effect::Copy, formats}));
    static_cast<void>(Desktop::TestHooks::enqueueDragDrop(target, DDEvents::Moved{{1}, {1, 1}, {1}, {1}, DD::Effect::Copy, formats}));
    static_cast<void>(Desktop::TestHooks::enqueueDragDrop(target, DDEvents::Moved{{1}, {2, 2}, {1}, {2}, DD::Effect::Copy, formats}));
    static_cast<void>(context.expectEq("compatible movement coalesces", std::size_t{2}, target.eventQueueInfo().pendingEvents));
    static_cast<void>(Desktop::TestHooks::enqueueDragDrop(target, DDEvents::Left{{1}}));
    static_cast<void>(context.expectEq("queue is full before terminal insertion", std::size_t{3}, target.eventQueueInfo().pendingEvents));
    Transfer::Payload payload;
    payload.push_back(Transfer::Text{"terminal"});
    static_cast<void>(Desktop::TestHooks::enqueueDragDrop(target, DDEvents::Dropped{{1}, {2, 2}, {2}, DD::Effect::Copy, std::move(payload)}, true));
    static_cast<void>(Desktop::TestHooks::enqueueDragDrop(target, DDEvents::Left{{1}}));
    static_cast<void>(context.expectEq("terminal eviction and later noise are counted", std::uint64_t{2}, target.eventQueueInfo().droppedEvents));
    std::array<DD::Event, 3> popped;
    static_cast<void>(context.expectEq("all retained events pop", std::size_t{3}, target.popEvents(popped)));
    static_cast<void>(context.expectTrue(
        "terminal drop remains retained",
        std::ranges::any_of(
            popped,
            [](const auto &event)
            {
                return event.template getIf<DDEvents::Dropped>() != nullptr;
            })));
    static_cast<void>(context.expectTrue(
        "terminal insertion evicts movement before meaningful events",
        std::ranges::none_of(
            popped,
            [](const auto &event)
            {
                return event.template getIf<DDEvents::Moved>() != nullptr;
            })));
    static_cast<void>(context.expectTrue("retained queue remains FIFO at the head", popped[0].getIf<DDEvents::Entered>() != nullptr));
    static_cast<void>(context.expectTrue("retained queue remains FIFO through meaningful events", popped[1].getIf<DDEvents::Left>() != nullptr));
    static_cast<void>(context.expectTrue("terminal event is appended at the FIFO tail", popped[2].getIf<DDEvents::Dropped>() != nullptr));
    target.clearDroppedEventCount();
    static_cast<void>(context.expectEq("dropped counter clears", std::uint64_t{0}, target.eventQueueInfo().droppedEvents));

    const auto pumpQueued = Desktop::TestHooks::routeDragDropDuringPump(target, DDEvents::Entered{{2}, {}, {1}, DD::Effect::Copy, formats});
    static_cast<void>(context.expectEq("pump counts queued DragDrop event", std::uint64_t{1}, pumpQueued.eventsQueued));
    static_cast<void>(Desktop::TestHooks::enqueueDragDrop(target, DDEvents::Left{{2}}));
    static_cast<void>(Desktop::TestHooks::enqueueDragDrop(target, DDEvents::Entered{{3}, {}, {1}, DD::Effect::Copy, formats}));
    const auto pumpDropped = Desktop::TestHooks::routeDragDropDuringPump(target, DDEvents::Left{{3}});
    static_cast<void>(context.expectEq("pump counts dropped DragDrop event", std::uint64_t{1}, pumpDropped.eventsDropped));
    target.clearEvents();
    static_cast<void>(context.expectEq("clear releases all retained queue entries", std::size_t{0}, target.eventQueueInfo().pendingEvents));

    static_cast<void>(Desktop::TestHooks::enqueueDragDrop(target, DDEvents::Moved{{4}, {}, {1}, {2}, DD::Effect::Copy, formats}));
    static_cast<void>(Desktop::TestHooks::enqueueDragDrop(target, DDEvents::Moved{{4}, {}, {2}, {3}, DD::Effect::Copy, formats}));
    DD::Event coalescedMove;
    static_cast<void>(context.expectTrue("coalesced movement pops", target.popEvent(coalescedMove)));
    const auto *coalesced = coalescedMove.getIf<DDEvents::Moved>();
    static_cast<void>(context.expectTrue("coalesced movement keeps its payload type", coalesced != nullptr));
    if (coalesced != nullptr)
        static_cast<void>(context.expectEq("coalescing preserves earliest previous region", DD::RegionId{1}, coalesced->previousRegion));
    static_cast<void>(Desktop::TestHooks::enqueueDragDrop(target, DDEvents::Moved{{5}, {}, {}, {1}, DD::Effect::Copy, formats}));
    static_cast<void>(Desktop::TestHooks::enqueueDragDrop(target, DDEvents::Moved{{6}, {}, {}, {1}, DD::Effect::Copy, formats}));
    static_cast<void>(context.expectEq("unrelated session moves do not coalesce", std::size_t{2}, target.eventQueueInfo().pendingEvents));
    target.clearEvents();

    auto releasableFormats =
        std::make_shared<const std::vector<Transfer::Format>>(std::vector<Transfer::Format>{{Transfer::FormatKind::Custom, std::string(2048, 'x')}});
    std::weak_ptr<const std::vector<Transfer::Format>> releasedFormats = releasableFormats;
    static_cast<void>(Desktop::TestHooks::enqueueDragDrop(target, DDEvents::Entered{{7}, {}, {}, DD::Effect::None, releasableFormats}));
    releasableFormats.reset();
    DD::Event releasedEvent;
    static_cast<void>(context.expectTrue("retained event pops", target.popEvent(releasedEvent)));
    releasedEvent = {};
    static_cast<void>(context.expectTrue("pop clears retained slot storage", releasedFormats.expired()));
    auto clearableFormats =
        std::make_shared<const std::vector<Transfer::Format>>(std::vector<Transfer::Format>{{Transfer::FormatKind::Custom, std::string(2048, 'y')}});
    std::weak_ptr<const std::vector<Transfer::Format>> clearedFormats = clearableFormats;
    static_cast<void>(Desktop::TestHooks::enqueueDragDrop(target, DDEvents::Entered{{8}, {}, {}, DD::Effect::None, clearableFormats}));
    clearableFormats.reset();
    target.clearEvents();
    static_cast<void>(context.expectTrue("clear releases retained slot storage", clearedFormats.expired()));

    std::string copiedCustomName = "GameWIP.FormatAlpha";
    std::array<Transfer::FormatView, 1> copiedCustomFormat{{{Transfer::FormatKind::Custom, copiedCustomName}}};
    std::array<DD::RegionDescription, 1> copiedRegions{{{DD::RegionId{9}, std::nullopt, copiedCustomFormat, DD::Effect::Copy, DD::Effect::Copy}}};
    static_cast<void>(context.expectTrue("setRegions copies caller views", target.setRegions(copiedRegions).ok()));
    copiedCustomName.assign("GameWIP.FormatBravo");
    std::array<Transfer::FormatView, 1> originalCustomFormat{{{Transfer::FormatKind::Custom, "GameWIP.FormatAlpha"}}};
    static_cast<void>(context.expectEq(
        "published custom identity is independent of caller storage",
        DD::RegionId{9},
        Desktop::TestHooks::matchDragDropRegion(target, {}, originalCustomFormat)));

    const auto invalidReplacement = target.setRegions(duplicateRegions);
    static_cast<void>(context.expectEq("invalid replacement is rejected transactionally", ErrorCode::InvalidArgument, invalidReplacement.code));
    static_cast<void>(
        context.expectEq("failed replacement preserves prior snapshot", std::size_t{1}, Desktop::TestHooks::dragDropRegionCount(target)));
    static_cast<void>(context.expectTrue("target survives invalid replacement", target.isOpen()));
    Desktop::TestHooks::failNext(Desktop::TestHooks::FailurePoint::DragDropRevocation);
    static_cast<void>(context.expectEq("revocation failure is reported", ErrorCode::CloseFailed, target.close().code));
    static_cast<void>(context.expectTrue("failed revocation retains an open retryable target", target.isOpen()));
    static_cast<void>(context.expectTrue("target closes on revocation retry", target.close().ok()));
    static_cast<void>(context.expectTrue("lightweight mode enables after target close", window.setFileDropEnabled(true).ok()));
    static_cast<void>(context.expectEq("full target conflicts with lightweight mode", ErrorCode::ResourceBusy, target.open(window, {}).code));
    static_cast<void>(context.expectTrue("lightweight mode disables for migration", window.setFileDropEnabled(false).ok()));
    std::array<DD::Event, 2> externalStorage;
    static_cast<void>(
        context.expectTrue("target reopens with external queue", target.open(window, DD::TargetDescription{regions}, externalStorage).ok()));
    static_cast<void>(
        context.expectEq("external queue ownership is reported", Desktop::Types::Events::StorageKind::External, target.eventQueueInfo().storage));
    ErrorCode wrongThreadClose = ErrorCode::Unknown;
    ErrorCode wrongThreadRegions = ErrorCode::Unknown;
    bool wrongThreadPop = true;
    std::thread wrongThread(
        [&]
        {
            wrongThreadClose = target.close().code;
            wrongThreadRegions = target.setRegions(regions).code;
            DD::Event event;
            wrongThreadPop = target.popEvent(event);
        });
    wrongThread.join();
    static_cast<void>(context.expectEq("wrong-thread target close is busy", ErrorCode::ResourceBusy, wrongThreadClose));
    static_cast<void>(context.expectEq("wrong-thread region replacement is busy", ErrorCode::ResourceBusy, wrongThreadRegions));
    static_cast<void>(context.expectFalse("wrong-thread event consumption is rejected", wrongThreadPop));
    Desktop::TestHooks::failNext(Desktop::TestHooks::FailurePoint::DragDropRevocation);
    static_cast<void>(context.expectEq("Window close reports target revocation failure", ErrorCode::CloseFailed, window.close().code));
    static_cast<void>(context.expectTrue("failed Window close preserves the native target", target.isOpen()));
    static_cast<void>(context.expectTrue("failed Window close preserves the Window", window.isOpen()));
    const Desktop::Types::WindowId retainedWindowId = window.id();
    static_cast<void>(context.expectTrue("unexpected native Window destruction succeeds", Desktop::TestHooks::destroyNativeWindow(window).ok()));
    static_cast<void>(context.expectFalse("Window closure invalidates target native access", target.isOpen()));
    static_cast<void>(context.expectEq(
        "Window closure leaves target pending finalization",
        Desktop::Types::LifetimeState::NativeDestroyedPendingFinalize,
        target.lifetimeState()));
    static_cast<void>(context.expectEq("pending target retains Window identity", retainedWindowId, target.windowId()));
    static_cast<void>(context.expectTrue("parent-loss target finalizes", target.close().ok()));
    static_cast<void>(context.expectTrue("Window lifetime finalizes after target", window.close().ok()));

    Desktop::Window deferredWindowA;
    Desktop::Window deferredWindowB;
    if (context.expectTrue("first deferred-cleanup Window opens", deferredWindowA.open(windowDescription, 4).ok()) &&
        context.expectTrue("second deferred-cleanup Window opens", deferredWindowB.open(windowDescription, 4).ok()))
    {
        auto deferredTargetA = std::make_unique<Desktop::DragDropTarget>();
        auto deferredTargetB = std::make_unique<Desktop::DragDropTarget>();
        if (context.expectTrue(
                "first deferred-cleanup target opens",
                deferredTargetA->open(deferredWindowA, DD::TargetDescription{regions}, 2).ok()) &&
            context.expectTrue(
                "second deferred-cleanup target opens",
                deferredTargetB->open(deferredWindowB, DD::TargetDescription{regions}, 2).ok()))
        {
            std::thread destroyOnWrongThread(
                [first = std::move(deferredTargetA), second = std::move(deferredTargetB)]() mutable
                {
                    first.reset();
                    second.reset();
                });
            destroyOnWrongThread.join();
            static_cast<void>(context.expectEq(
                "both wrong-thread targets enter deferred ownership",
                std::size_t{2},
                Desktop::TestHooks::deferredDragDropTargetCount()));
            Desktop::TestHooks::failNext(Desktop::TestHooks::FailurePoint::DragDropRevocation);
            static_cast<void>(Desktop::Events::poll());
            static_cast<void>(context.expectEq(
                "one failed cleanup preserves the complete deferred chain",
                std::size_t{2},
                Desktop::TestHooks::deferredDragDropTargetCount()));
            static_cast<void>(context.expectEq(
                "one failed cleanup preserves both active registrations",
                std::size_t{2},
                Desktop::TestHooks::activeDragDropTargetCount()));
            static_cast<void>(Desktop::Events::poll());
            static_cast<void>(
                context.expectEq("deferred retry processes every target", std::size_t{0}, Desktop::TestHooks::deferredDragDropTargetCount()));
            static_cast<void>(
                context.expectEq("deferred retry empties the active registry", std::size_t{0}, Desktop::TestHooks::activeDragDropTargetCount()));
            static_cast<void>(
                context.expectTrue("first Window accepts lightweight mode after deferred cleanup", deferredWindowA.setFileDropEnabled(true).ok()));
            static_cast<void>(
                context.expectTrue("second Window accepts lightweight mode after deferred cleanup", deferredWindowB.setFileDropEnabled(true).ok()));
        }
        static_cast<void>(deferredWindowA.close());
        static_cast<void>(deferredWindowB.close());
    }
}
