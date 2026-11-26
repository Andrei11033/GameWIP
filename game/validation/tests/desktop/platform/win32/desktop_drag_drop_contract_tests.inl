/// @file desktop_drag_drop_contract_tests.inl
/// @brief DragDrop identity, format, and payload validation.

void testDragDropContracts(TestSupport::Context &context)
{
    namespace DD = Desktop::Types::DragDrop;
    namespace DDEvents = DD::Events;
    namespace Transfer = Desktop::Types::DataTransfer;

    // Validate identity, effect negotiation, and native completion results.
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
        Desktop::Internal::TestHooks::negotiateDragDropEffect(DD::Effect::Copy, DD::Effect::Move, DD::Effect::Move)));
    static_cast<void>(context.expectEq(
        "single effect intersection wins",
        DD::Effect::Move,
        Desktop::Internal::TestHooks::negotiateDragDropEffect(all, DD::Effect::Move, DD::Effect::Move)));
    static_cast<void>(context.expectEq(
        "preferred candidate wins",
        DD::Effect::Link,
        Desktop::Internal::TestHooks::negotiateDragDropEffect(all, all, DD::Effect::Link)));
    static_cast<void>(context.expectEq(
        "fallback prefers Copy",
        DD::Effect::Copy,
        Desktop::Internal::TestHooks::negotiateDragDropEffect(all, all, DD::Effect::None)));
    static_cast<void>(context.expectEq(
        "fallback prefers Move when Copy is absent",
        DD::Effect::Move,
        Desktop::Internal::TestHooks::negotiateDragDropEffect(DD::Effect::Move | DD::Effect::Link, all, DD::Effect::None)));
    static_cast<void>(context.expectEq(
        "fallback selects Link when it is the only candidate",
        DD::Effect::Link,
        Desktop::Internal::TestHooks::negotiateDragDropEffect(DD::Effect::Link, all, DD::Effect::None)));
    static_cast<void>(context.expectEq(
        "invalid effect bits normalize to None",
        DD::Effect::None,
        Desktop::Internal::TestHooks::negotiateDragDropEffect(static_cast<DD::Effect>(0x80), all, DD::Effect::Copy)));

    const DD::Result rejectedDrop = Win32TestHooks::droppedDragDropSourceResult(DD::Effect::None, DD::Effect::Copy);
    if(context.expectTrue("rejected native drop completes successfully", rejectedDrop.ok()))
    {
        static_cast<void>(context.expectEq("rejected native drop is cancelled", DD::Outcome::Cancelled, rejectedDrop.value().outcome));
        static_cast<void>(context.expectEq("rejected native drop performs no effect", DD::Effect::None, rejectedDrop.value().effect));
    }

    const DD::Result acceptedDrop = Win32TestHooks::droppedDragDropSourceResult(DD::Effect::Move, all);
    if(context.expectTrue("accepted native drop completes successfully", acceptedDrop.ok()))
    {
        static_cast<void>(context.expectEq("accepted native drop is dropped", DD::Outcome::Dropped, acceptedDrop.value().outcome));
        static_cast<void>(context.expectEq("accepted native drop reports its effect", DD::Effect::Move, acceptedDrop.value().effect));
    }
    static_cast<void>(context.expectEq(
        "multiple native completion effects fail",
        ErrorCode::NativeFailure,
        Win32TestHooks::droppedDragDropSourceResult(DD::Effect::Copy | DD::Effect::Move, all).status().code()));
    static_cast<void>(context.expectEq(
        "unadvertised native completion effect fails",
        ErrorCode::NativeFailure,
        Win32TestHooks::droppedDragDropSourceResult(DD::Effect::Move, DD::Effect::Copy).status().code()));

    // Validate text payloads.
    std::array<Transfer::ItemView, 1> validTextItems{{Transfer::TextView{"text"}}};
    const DD::Description validSource{validTextItems, DD::EffectPolicy::copy(), DD::TriggerButton::Left};
    static_cast<void>(context.expectTrue("valid source data prepares", Win32TestHooks::prepareDragDropSource(validSource).ok()));
    static_cast<void>(context.expectEq("empty source is invalid", ErrorCode::InvalidArgument, Win32TestHooks::prepareDragDropSource({}).code()));
    DD::Description invalidSourceEffects = validSource;
    invalidSourceEffects.effects.allowed = static_cast<DD::Effect>(0x80);
    static_cast<void>(context.expectEq(
        "invalid source effect bits are rejected",
        ErrorCode::InvalidArgument,
        Win32TestHooks::prepareDragDropSource(invalidSourceEffects).code()));
    DD::Description invalidTrigger = validSource;
    invalidTrigger.triggerButton = static_cast<DD::TriggerButton>(99);
    static_cast<void>(context.expectEq(
        "invalid source trigger is rejected",
        ErrorCode::InvalidArgument,
        Win32TestHooks::prepareDragDropSource(invalidTrigger).code()));
    std::array<Transfer::ItemView, 2> duplicateTextItems{{Transfer::TextView{"one"}, Transfer::TextView{"two"}}};
    static_cast<void>(context.expectEq(
        "duplicate native source formats are rejected",
        ErrorCode::InvalidArgument,
        Win32TestHooks::prepareDragDropSource({duplicateTextItems, DD::EffectPolicy::copy(), DD::TriggerButton::Left}).code()));
    std::array<Transfer::ItemView, 1> malformedTextItems{{Transfer::TextView{std::string_view{"\xC3", 1}}}};
    static_cast<void>(context.expectEq(
        "malformed source UTF-8 is rejected",
        ErrorCode::InvalidArgument,
        Win32TestHooks::prepareDragDropSource({malformedTextItems, DD::EffectPolicy::copy(), DD::TriggerButton::Left}).code()));
    std::array<Transfer::ItemView, 1> nulTextItems{{Transfer::TextView{std::string_view{"a\0b", 3}}}};
    static_cast<void>(context.expectEq(
        "source text embedded NUL is rejected",
        ErrorCode::InvalidArgument,
        Win32TestHooks::prepareDragDropSource({nulTextItems, DD::EffectPolicy::copy(), DD::TriggerButton::Left}).code()));

    // Validate custom format names and image dimensions.
    std::array<std::byte, 1> oneByte{std::byte{1}};
    std::array<Transfer::ItemView, 1> malformedCustomItems{{Transfer::CustomView{std::string_view{"\xC3", 1}, oneByte}}};
    static_cast<void>(context.expectEq(
        "malformed source custom name is rejected",
        ErrorCode::InvalidArgument,
        Win32TestHooks::prepareDragDropSource({malformedCustomItems, DD::EffectPolicy::copy(), DD::TriggerButton::Left}).code()));
    std::array<Transfer::ItemView, 1> nulCustomItems{{Transfer::CustomView{std::string_view{"a\0b", 3}, oneByte}}};
    static_cast<void>(context.expectEq(
        "source custom name embedded NUL is rejected",
        ErrorCode::InvalidArgument,
        Win32TestHooks::prepareDragDropSource({nulCustomItems, DD::EffectPolicy::copy(), DD::TriggerButton::Left}).code()));
    std::array<Transfer::ItemView, 1> emptyCustomItems{{Transfer::CustomView{"GameWIP.Empty", {}}}};
    static_cast<void>(context.expectEq(
        "zero-byte custom source is unsupported",
        ErrorCode::Unsupported,
        Win32TestHooks::prepareDragDropSource({emptyCustomItems, DD::EffectPolicy::copy(), DD::TriggerButton::Left}).code()));

    std::array<std::byte, 3> invalidImageBytes{};
    std::array<Transfer::ItemView, 1> invalidImageItems{{Transfer::ImageView::strided({1, 1}, 0, invalidImageBytes)}};
    static_cast<void>(context.expectEq(
        "invalid image extent is rejected",
        ErrorCode::InvalidArgument,
        Win32TestHooks::prepareDragDropSource({invalidImageItems, DD::EffectPolicy::copy(), DD::TriggerButton::Left}).code()));
    std::array<Transfer::ItemView, 1> zeroImageItems{{Transfer::ImageView::strided({0, 1}, 0, {})}};
    static_cast<void>(context.expectEq(
        "zero image dimension is rejected",
        ErrorCode::InvalidArgument,
        Win32TestHooks::prepareDragDropSource({zeroImageItems, DD::EffectPolicy::copy(), DD::TriggerButton::Left}).code()));
    std::array<std::byte, 4> onePixel{};
    std::array<Transfer::ItemView, 1> invalidStrideItems{{Transfer::ImageView::strided({1, 1}, 3, onePixel)}};
    static_cast<void>(context.expectEq(
        "undersized image stride is rejected",
        ErrorCode::InvalidArgument,
        Win32TestHooks::prepareDragDropSource({invalidStrideItems, DD::EffectPolicy::copy(), DD::TriggerButton::Left}).code()));

    // Validate source file paths.
    std::array<GameWIP::FileSystem::Types::Path, 1> relativePaths{{L"relative.txt"}};
    std::array<Transfer::ItemView, 1> relativeFileItems{{Transfer::FileListView{relativePaths}}};
    static_cast<void>(context.expectEq(
        "relative source file path is rejected",
        ErrorCode::InvalidArgument,
        Win32TestHooks::prepareDragDropSource({relativeFileItems, DD::EffectPolicy::copy(), DD::TriggerButton::Left}).code()));
    std::wstring malformedNativePath = L"C:\\";
    malformedNativePath.push_back(static_cast<wchar_t>(0xD800));
    std::array<GameWIP::FileSystem::Types::Path, 1> malformedPaths{{malformedNativePath}};
    std::array<Transfer::ItemView, 1> malformedFileItems{{Transfer::FileListView{malformedPaths}}};
    static_cast<void>(context.expectEq(
        "malformed native Unicode source path is rejected",
        ErrorCode::InvalidArgument,
        Win32TestHooks::prepareDragDropSource({malformedFileItems, DD::EffectPolicy::copy(), DD::TriggerButton::Left}).code()));

    // Verify preparation allocation failure.
    Desktop::Internal::TestHooks::failNext(Desktop::Internal::TestHooks::FailurePoint::DragDropPreparation);
    static_cast<void>(context.expectEq(
        "source preparation failure is deterministic",
        ErrorCode::OutOfMemory,
        Win32TestHooks::prepareDragDropSource(validSource).code()));

    // Check apartment compatibility and native source materialization failures.
    Desktop::Internal::TestHooks::failNext(Desktop::Internal::TestHooks::FailurePoint::DragDropOleInitialization);
    static_cast<void>(context.expectEq(
        "source OLE initialization failure is deterministic",
        ErrorCode::OpenFailed,
        Win32TestHooks::testDragDropOleInitialization().code()));
    ErrorCode compatiblePreinitialized = ErrorCode::Unknown;
    ErrorCode incompatibleApartment = ErrorCode::Unknown;
    std::thread compatibleApartmentThread(
        [&]
        {
            const HRESULT initialized = OleInitialize(nullptr);

            if(initialized == S_OK || initialized == S_FALSE)
            {
                compatiblePreinitialized = Win32TestHooks::testDragDropOleInitialization().code();
            }

            if(initialized == S_OK || initialized == S_FALSE)
            {
                OleUninitialize();
            }
        });
    compatibleApartmentThread.join();
    static_cast<void>(context.expectEq("compatible preinitialized OLE apartment is reused", ErrorCode::Success, compatiblePreinitialized));
    std::thread incompatibleApartmentThread(
        [&]
        {
            const HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);

            if(initialized == S_OK || initialized == S_FALSE)
            {
                incompatibleApartment = Win32TestHooks::testDragDropOleInitialization().code();
            }

            if(initialized == S_OK || initialized == S_FALSE)
            {
                CoUninitialize();
            }
        });
    incompatibleApartmentThread.join();
    static_cast<void>(context.expectEq("incompatible OLE apartment is ResourceBusy", ErrorCode::ResourceBusy, incompatibleApartment));
    Desktop::Internal::TestHooks::failNext(Desktop::Internal::TestHooks::FailurePoint::DragDropMaterialization);
    static_cast<void>(context.expectEq(
        "target materialization failure is deterministic",
        ErrorCode::ReadFailed,
        Win32TestHooks::testDragDropMaterialization().code()));
    static_cast<void>(context.expectTrue("source COM data-object and enumerator contracts hold", Win32TestHooks::dragDropComContractsValid()));
}
