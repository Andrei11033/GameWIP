/// @file desktop_cursor_native_tests.inl
/// @brief Native cursor resource and Window integration tests.

/// ------------------------------------------------------------
/// Native cursor resources
/// ------------------------------------------------------------

#if DESKTOP_INTERNAL_TEST_HOOKS

void testCursorNativeResources(TestSupport::Context &context)
{
    // Verify native resource sharing and final release.
    Desktop::Internal::TestHooks::resetFailures();
    const CursorTest::Pixels pixels = CursorTest::pixels();
    const std::size_t createdBefore = Desktop::Internal::TestHooks::createdCustomCursorCount();
    const std::size_t destroyedBefore = Desktop::Internal::TestHooks::destroyedCustomCursorCount();
    {
        auto one = CursorTest::create(pixels);
        if(!context.expectTrue("one native cursor variant creates", one.ok()))
        {
            return;
        }

        static_cast<void>(
            context.expectEq("one native variant is retained", std::size_t{1}, Desktop::Internal::TestHooks::customCursorVariantCount(one.value())));
        const auto snapshot = Desktop::Internal::TestHooks::inspectCustomCursorVariant(one.value(), 0);
        static_cast<void>(context.expectTrue("native cursor variant can be inspected", snapshot.valid));
        static_cast<void>(context.expectEq("custom cursor hotspot x reaches Win32", std::uint32_t{1}, snapshot.hotspot.x));
        static_cast<void>(context.expectEq("custom cursor hotspot y reaches Win32", std::uint32_t{1}, snapshot.hotspot.y));
        static_cast<void>(context.expectEq(
            "RGBA cursor pixels convert to BGRA",
            std::array<std::byte, 4>{std::byte{0x33}, std::byte{0x22}, std::byte{0x11}, std::byte{0x44}},
            snapshot.firstBgraPixel));

        // Intentional copy: this verifies that sharing a cursor does not duplicate native variants.
        // NOLINTNEXTLINE(performance-unnecessary-copy-initialization)
        const Desktop::Cursor shared = one.value();
        static_cast<void>(context.expectEq(
            "copying a Cursor does not duplicate native variants",
            createdBefore + 1,
            Desktop::Internal::TestHooks::createdCustomCursorCount()));
        static_cast<void>(shared.isValid());
    }
    static_cast<void>(context.expectEq(
        "last Cursor reference destroys its native variant",
        destroyedBefore + 1,
        Desktop::Internal::TestHooks::destroyedCustomCursorCount()));

    // Verify eager DPI variants and rollback after partial creation.
    std::array images{CursorTest::image(pixels, 96), CursorTest::image(pixels, 144), CursorTest::image(pixels, 192)};
    auto multiple = Desktop::createCursor(images);
    if(!context.expectTrue("multiple native cursor variants create", multiple.ok()))
    {
        return;
    }

    static_cast<void>(context.expectEq(
        "all native variants are eagerly retained",
        std::size_t{3},
        Desktop::Internal::TestHooks::customCursorVariantCount(multiple.value())));

    const std::size_t partialCreated = Desktop::Internal::TestHooks::createdCustomCursorCount();
    const std::size_t partialDestroyed = Desktop::Internal::TestHooks::destroyedCustomCursorCount();
    Desktop::Internal::TestHooks::failCursorNativeCreationAfter(1);
    auto partial = Desktop::createCursor(images);
    static_cast<void>(
        context.expectEq("injected native cursor failure is translated", GameWIP::Status::Types::Code::NativeFailure, partial.status().code()));
    static_cast<void>(context.expectFalse("partial native creation publishes no Cursor", partial.hasValue() && partial.value().isValid()));
    static_cast<void>(context.expectEq(
        "partial native creation made exactly one candidate",
        partialCreated + 1,
        Desktop::Internal::TestHooks::createdCustomCursorCount()));
    static_cast<void>(context.expectEq(
        "partial native creation cleans its earlier candidate",
        partialDestroyed + 1,
        Desktop::Internal::TestHooks::destroyedCustomCursorCount()));

    // Verify allocation failures publish no partial Cursor.
    const std::size_t allocationCreated = Desktop::Internal::TestHooks::createdCustomCursorCount();
    Desktop::Internal::TestHooks::failNext(Desktop::Internal::TestHooks::FailurePoint::Allocation);
    auto allocation = CursorTest::create(pixels);
    static_cast<void>(
        context.expectEq("cursor allocation failure is translated", GameWIP::Status::Types::Code::OutOfMemory, allocation.status().code()));
    static_cast<void>(context.expectFalse("cursor allocation failure publishes no Cursor", allocation.hasValue() && allocation.value().isValid()));
    static_cast<void>(context.expectEq(
        "allocation failure creates no native candidate",
        allocationCreated,
        Desktop::Internal::TestHooks::createdCustomCursorCount()));

    const std::size_t stateAllocationCreated = Desktop::Internal::TestHooks::createdCustomCursorCount();
    const std::size_t stateAllocationDestroyed = Desktop::Internal::TestHooks::destroyedCustomCursorCount();
    Desktop::Internal::TestHooks::failNext(Desktop::Internal::TestHooks::FailurePoint::CursorStateAllocation);
    auto stateAllocation = Desktop::createCursor(images);
    static_cast<void>(
        context.expectEq("CursorState allocation failure is translated", GameWIP::Status::Types::Code::OutOfMemory, stateAllocation.status().code()));
    static_cast<void>(
        context.expectFalse("CursorState allocation failure publishes no Cursor", stateAllocation.hasValue() && stateAllocation.value().isValid()));
    static_cast<void>(context.expectEq(
        "CursorState allocation failure materializes all candidates first",
        stateAllocationCreated + images.size(),
        Desktop::Internal::TestHooks::createdCustomCursorCount()));
    static_cast<void>(context.expectEq(
        "CursorState allocation failure cleans all native candidates",
        stateAllocationDestroyed + images.size(),
        Desktop::Internal::TestHooks::destroyedCustomCursorCount()));
    Desktop::Internal::TestHooks::resetFailures();
}

void testCursorWindowIntegration(TestSupport::Context &context)
{
    // Create shared resources and bind them to a hidden Window.
    const CursorTest::Pixels pixels = CursorTest::pixels();
    auto first = CursorTest::create(pixels, 96);
    auto second = CursorTest::create(pixels, 144);
    if(!context.expectTrue("cursor integration resources create", first.ok() && second.ok()))
    {
        return;
    }
    if(!context.expectTrue("cursor integration resources are valid", first.value().isValid() && second.value().isValid()))
    {
        return;
    }

    Desktop::Window closed;
    static_cast<void>(context.expectEq(
        "setting a Cursor on a closed Window reports NotOpen",
        GameWIP::Status::Types::Code::NotOpen,
        Desktop::setCursor(closed, first.value()).status().code()));
    static_cast<void>(context.expectFalse("closed Window has no custom Cursor", Desktop::hasCustomCursor(closed)));

    Desktop::Types::Description description;
    description.title = "GameWIP custom cursor integration";
    description.activation.visible = false;
    Desktop::Window window;
    if(!context.expectTrue("custom cursor fixture opens", window.open(description, Desktop::Events::owned(8)).ok()))
    {
        return;
    }

    const Desktop::Cursor invalid;
    static_cast<void>(context.expectEq(
        "invalid Cursor selection is rejected",
        GameWIP::Status::Types::Code::InvalidArgument,
        Desktop::setCursor(window, invalid).status().code()));
    static_cast<void>(context.expectTrue("first custom Cursor selection succeeds", Desktop::setCursor(window, first.value()).ok()));
    static_cast<void>(context.expectTrue("custom Cursor binding is reported", Desktop::hasCustomCursor(window)));
    static_cast<void>(
        context.expectEq("custom Cursor leaves cached fallback shape unchanged", Desktop::Types::CursorShape::Arrow, window.cursorShape()));
    static_cast<void>(context.expectTrue("custom-to-custom replacement succeeds", Desktop::setCursor(window, second.value()).ok()));
    static_cast<void>(
        context.expectEq("replacement selects its intended DPI", std::uint32_t{144}, Desktop::Internal::TestHooks::customCursorBindingDpi(window)));

    Desktop::Window sharedWindow;
    if(!context.expectTrue("second custom cursor fixture opens", sharedWindow.open(description, Desktop::Events::owned(8)).ok()))
    {
        static_cast<void>(window.close());
        return;
    }

    // Verify sharing, transactional updates, and thread affinity.
    const std::size_t createdBeforeSharing = Desktop::Internal::TestHooks::createdCustomCursorCount();
    static_cast<void>(context.expectTrue("one Cursor can bind a second Window", Desktop::setCursor(sharedWindow, second.value()).ok()));
    static_cast<void>(
        context.expectTrue("both Windows retain the same custom Cursor", Desktop::hasCustomCursor(window) && Desktop::hasCustomCursor(sharedWindow)));
    static_cast<void>(context.expectEq(
        "sharing a Cursor across Windows creates no native variant",
        createdBeforeSharing,
        Desktop::Internal::TestHooks::createdCustomCursorCount()));

    Desktop::Internal::TestHooks::failNext(Desktop::Internal::TestHooks::FailurePoint::Allocation);
    static_cast<void>(context.expectEq(
        "binding allocation failure is translated",
        GameWIP::Status::Types::Code::OutOfMemory,
        Desktop::setCursor(window, first.value()).status().code()));
    static_cast<void>(context.expectEq(
        "binding allocation failure preserves previous custom selection",
        std::uint32_t{144},
        Desktop::Internal::TestHooks::customCursorBindingDpi(window)));

    Desktop::Internal::TestHooks::failNext(Desktop::Internal::TestHooks::FailurePoint::CursorBinding);
    static_cast<void>(context.expectEq(
        "binding failure is transactional",
        GameWIP::Status::Types::Code::NativeFailure,
        Desktop::setCursor(window, first.value()).status().code()));
    static_cast<void>(context.expectEq(
        "failed binding preserves previous custom selection",
        std::uint32_t{144},
        Desktop::Internal::TestHooks::customCursorBindingDpi(window)));

    std::atomic<GameWIP::Status::Types::Code> wrongThreadCode{GameWIP::Status::Types::Code::Success};
    std::thread wrongThread(
        [&]
        {
            wrongThreadCode.store(Desktop::setCursor(window, first.value()).status().code(), std::memory_order_relaxed);
        });
    wrongThread.join();
    static_cast<void>(
        context.expectEq("wrong-thread Cursor selection reports ResourceBusy", GameWIP::Status::Types::Code::ResourceBusy, wrongThreadCode.load()));

    std::atomic<bool> wrongThreadHasCustomCursor{true};
    std::thread wrongThreadQuery(
        [&]
        {
            wrongThreadHasCustomCursor.store(Desktop::hasCustomCursor(window), std::memory_order_relaxed);
        });
    wrongThreadQuery.join();
    static_cast<void>(context.expectFalse("wrong-thread custom Cursor query reports false", wrongThreadHasCustomCursor.load()));

    // Verify system fallback and retained binding across cursor modes.
    Desktop::Internal::TestHooks::failNext(Desktop::Internal::TestHooks::FailurePoint::SystemCursorLoad);
    static_cast<void>(context.expectEq(
        "system cursor load failure is transactional",
        ErrorCode::NativeFailure,
        window.setCursorShape(Desktop::Types::CursorShape::Hand).status().code()));
    static_cast<void>(context.expectTrue("failed system fallback preserves custom binding", Desktop::hasCustomCursor(window)));
    static_cast<void>(context.expectEq("failed system fallback preserves cached shape", Desktop::Types::CursorShape::Arrow, window.cursorShape()));

    Desktop::Internal::TestHooks::failNext(Desktop::Internal::TestHooks::FailurePoint::CursorBinding);
    static_cast<void>(context.expectEq(
        "custom binding removal failure is transactional",
        ErrorCode::NativeFailure,
        window.setCursorShape(Desktop::Types::CursorShape::Hand).status().code()));
    static_cast<void>(context.expectTrue("failed custom binding removal preserves custom binding", Desktop::hasCustomCursor(window)));
    static_cast<void>(
        context.expectEq("failed custom binding removal preserves cached shape", Desktop::Types::CursorShape::Arrow, window.cursorShape()));

    static_cast<void>(
        context.expectTrue("system cursor shape replaces custom Cursor", window.setCursorShape(Desktop::Types::CursorShape::Hand).ok()));
    static_cast<void>(context.expectFalse("system cursor shape clears custom binding", Desktop::hasCustomCursor(window)));
    static_cast<void>(context.expectEq("system cursor replacement updates cached shape", Desktop::Types::CursorShape::Hand, window.cursorShape()));

    static_cast<void>(context.expectTrue("custom Cursor can be selected again", Desktop::setCursor(window, first.value()).ok()));
    first.value() = Desktop::Cursor{};
    static_cast<void>(context.expectTrue("Window retains Cursor after caller releases it", Desktop::hasCustomCursor(window)));
    static_cast<void>(context.expectTrue("hidden mode keeps custom binding", window.setCursorMode(Desktop::Types::CursorMode::Hidden).ok()));
    static_cast<void>(context.expectTrue("custom binding remains retained while hidden", Desktop::hasCustomCursor(window)));
    static_cast<void>(
        context.expectTrue("normal mode restores effective custom cursor", window.setCursorMode(Desktop::Types::CursorMode::Normal).ok()));
    static_cast<void>(context.expectTrue("relative mode keeps custom binding", window.setCursorMode(Desktop::Types::CursorMode::Relative).ok()));
    static_cast<void>(context.expectTrue("custom binding remains retained while relative", Desktop::hasCustomCursor(window)));
    static_cast<void>(context.expectTrue("leaving relative mode succeeds", window.setCursorMode(Desktop::Types::CursorMode::Normal).ok()));

    static_cast<void>(context.expectTrue("second Window reports shared custom Cursor", Desktop::hasCustomCursor(sharedWindow)));
    static_cast<void>(context.expectTrue("second custom cursor fixture closes", sharedWindow.close().ok()));

    static_cast<void>(context.expectTrue("custom cursor fixture closes", window.close().ok()));
    static_cast<void>(context.expectFalse("closed Window has no stale custom binding", Desktop::hasCustomCursor(window)));
    static_cast<void>(context.expectTrue("closed Window reopens", window.open(description, Desktop::Events::owned(8)).ok()));
    static_cast<void>(context.expectFalse("reopened Window has no stale custom binding", Desktop::hasCustomCursor(window)));
    static_cast<void>(context.expectTrue("reopened custom cursor fixture closes", window.close().ok()));
}

void testCursorLifecycle(TestSupport::Context &context)
{
    // Verify prebuilt DPI selection and normal close cleanup.
    const CursorTest::Pixels pixels = CursorTest::pixels();
    std::array images{CursorTest::image(pixels, 96), CursorTest::image(pixels, 144)};
    auto cursor = Desktop::createCursor(images);
    if(!context.expectTrue("DPI cursor resource creates", cursor.ok()))
    {
        return;
    }
    if(!context.expectTrue("DPI cursor resource is valid", cursor.value().isValid()))
    {
        return;
    }

    Desktop::Types::Description description;
    description.title = "GameWIP custom cursor lifecycle";
    description.activation.visible = false;
    Desktop::Window window;
    if(!context.expectTrue("DPI cursor fixture opens", window.open(description, Desktop::Events::owned(8)).ok()))
    {
        return;
    }

    if(!context.expectTrue("DPI cursor binds", Desktop::setCursor(window, cursor.value()).ok()))
    {
        static_cast<void>(window.close());
        return;
    }
    const auto native = Desktop::Native::Win32::getHandle(window);
    if(!context.expectTrue("DPI cursor fixture exposes its native handle", native.ok() && native.value().window != nullptr))
    {
        static_cast<void>(window.close());
        return;
    }

    RECT suggested{};
    static_cast<void>(GetWindowRect(native.value().window, &suggested));
    static_cast<void>(SendMessageW(native.value().window, WM_DPICHANGED, MAKEWPARAM(120, 120), reinterpret_cast<LPARAM>(&suggested)));
    static_cast<void>(context.expectEq(
        "DPI transition tie chooses higher prebuilt variant",
        std::uint32_t{144},
        Desktop::Internal::TestHooks::customCursorBindingDpi(window)));
    static_cast<void>(SendMessageW(native.value().window, WM_DPICHANGED, MAKEWPARAM(100, 100), reinterpret_cast<LPARAM>(&suggested)));
    static_cast<void>(context.expectEq(
        "DPI transition chooses nearest prebuilt variant",
        std::uint32_t{96},
        Desktop::Internal::TestHooks::customCursorBindingDpi(window)));
    const std::size_t createdBeforeReselect = Desktop::Internal::TestHooks::createdCustomCursorCount();
    static_cast<void>(SendMessageW(native.value().window, WM_DPICHANGED, MAKEWPARAM(144, 144), reinterpret_cast<LPARAM>(&suggested)));
    static_cast<void>(context.expectEq(
        "DPI transition chooses exact prebuilt variant",
        std::uint32_t{144},
        Desktop::Internal::TestHooks::customCursorBindingDpi(window)));
    static_cast<void>(context.expectEq(
        "DPI reselection creates no native resource",
        createdBeforeReselect,
        Desktop::Internal::TestHooks::createdCustomCursorCount()));
    const std::size_t destroyedBeforeNormal = Desktop::Internal::TestHooks::destroyedCustomCursorCount();
    cursor.value() = Desktop::Cursor{};
    static_cast<void>(context.expectTrue("DPI cursor fixture closes", window.close().ok()));
    static_cast<void>(context.expectEq(
        "normal close releases the final custom cursor binding",
        destroyedBeforeNormal + images.size(),
        Desktop::Internal::TestHooks::destroyedCustomCursorCount()));

    // Verify binding cleanup after native and deferred Window destruction.
    const std::size_t destroyedBeforeUnexpected = Desktop::Internal::TestHooks::destroyedCustomCursorCount();
    auto unexpectedCursor = CursorTest::create(pixels);
    if(!context.expectTrue("unexpected-destruction cursor resource creates", unexpectedCursor.ok()))
    {
        return;
    }

    Desktop::Window unexpected;
    if(!context.expectTrue("unexpected-destruction cursor fixture opens", unexpected.open(description, Desktop::Events::owned(4)).ok()))
    {
        return;
    }

    if(!context.expectTrue("unexpected-destruction cursor binds", Desktop::setCursor(unexpected, unexpectedCursor.value()).ok()))
    {
        static_cast<void>(unexpected.close());
        return;
    }
    unexpectedCursor.value() = Desktop::Cursor{};
    static_cast<void>(context.expectTrue("native Window destruction succeeds", Desktop::Internal::TestHooks::destroyNativeWindow(unexpected).ok()));
    static_cast<void>(context.expectEq(
        "unexpected native destruction releases custom cursor binding",
        destroyedBeforeUnexpected + 1,
        Desktop::Internal::TestHooks::destroyedCustomCursorCount()));
    static_cast<void>(context.expectTrue("unexpected native destruction finalizes", unexpected.close().ok()));

    const std::size_t destroyedBeforeDeferred = Desktop::Internal::TestHooks::destroyedCustomCursorCount();
    auto deferredCursor = CursorTest::create(pixels);
    if(!context.expectTrue("deferred cursor resource creates", deferredCursor.ok()))
    {
        return;
    }

    auto deferred = std::make_unique<Desktop::Window>();
    if(!context.expectTrue("deferred cursor fixture opens", deferred->open(description, Desktop::Events::owned(4)).ok()))
    {
        return;
    }

    if(!context.expectTrue("deferred cursor binds", Desktop::setCursor(*deferred, deferredCursor.value()).ok()))
    {
        static_cast<void>(deferred->close());
        return;
    }
    deferredCursor.value() = Desktop::Cursor{};
    std::thread destroyer(
        [owned = std::move(deferred)] mutable
        {
            owned.reset();
        });
    destroyer.join();
    static_cast<void>(context.expectTrue("owner pump drains deferred cursor cleanup", Desktop::Events::poll().ok()));
    static_cast<void>(context.expectEq(
        "deferred cleanup releases custom cursor binding",
        destroyedBeforeDeferred + 1,
        Desktop::Internal::TestHooks::destroyedCustomCursorCount()));
}
#endif
