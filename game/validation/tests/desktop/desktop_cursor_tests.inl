/// @file desktop_cursor_tests.inl
/// @brief Custom native cursor portable policy validation cases.

namespace CursorTest
{
    using Pixels = std::array<std::byte, 16>;

    [[nodiscard]] Pixels pixels(std::byte red = std::byte{0x11}, std::byte green = std::byte{0x22}, std::byte blue = std::byte{0x33})
    {
        return {
            red,
            green,
            blue,
            std::byte{0x44},
            std::byte{0x50},
            std::byte{0x60},
            std::byte{0x70},
            std::byte{0x80},
            std::byte{0x90},
            std::byte{0xA0},
            std::byte{0xB0},
            std::byte{0xC0},
            std::byte{0xD0},
            std::byte{0xE0},
            std::byte{0xF0},
            std::byte{0xFF}};
    }

    [[nodiscard]] Desktop::Types::Cursor::ImageView image(
        std::span<const std::byte> bytes,
        std::uint32_t dpi = 96,
        std::size_t stride = 0,
        Desktop::Types::Cursor::PixelPosition hotspot = {1, 1}) noexcept
    {
        return {{2, 2}, hotspot, dpi, stride, bytes};
    }

    [[nodiscard]] Desktop::Types::Cursor::CreateResult create(std::span<const std::byte> bytes, std::uint32_t dpi = 96)
    {
        const Desktop::Types::Cursor::ImageView variant = image(bytes, dpi);
        return Desktop::createCursor(variant);
    }
} // namespace CursorTest

void testCursorDpiSelection(TestSupport::Context &context)
{
    constexpr std::array<std::uint32_t, 3> orderedDpis{96, 144, 192};

    static_cast<void>(
        context.expectEq("cursor DPI selection chooses exact match", std::size_t{1}, Desktop::Detail::selectDpiVariantIndex(orderedDpis, 144)));

    static_cast<void>(context.expectEq(
        "cursor DPI selection clamps below supplied range",
        std::size_t{0},
        Desktop::Detail::selectDpiVariantIndex(orderedDpis, 72)));

    static_cast<void>(context.expectEq(
        "cursor DPI selection clamps above supplied range",
        std::size_t{2},
        Desktop::Detail::selectDpiVariantIndex(orderedDpis, 240)));

    static_cast<void>(context.expectEq(
        "cursor DPI selection chooses nearest lower variant",
        std::size_t{0},
        Desktop::Detail::selectDpiVariantIndex(orderedDpis, 110)));

    static_cast<void>(context.expectEq(
        "cursor DPI selection chooses nearest higher variant",
        std::size_t{2},
        Desktop::Detail::selectDpiVariantIndex(orderedDpis, 170)));

    static_cast<void>(context.expectEq(
        "cursor DPI selection chooses higher variant on equal distance",
        std::size_t{1},
        Desktop::Detail::selectDpiVariantIndex(orderedDpis, 120)));

    constexpr std::array<std::uint32_t, 3> unorderedDpis{192, 96, 144};

    static_cast<void>(context.expectEq(
        "cursor DPI selection does not require sorted variants",
        std::size_t{2},
        Desktop::Detail::selectDpiVariantIndex(unorderedDpis, 120)));
}

void testCursorValuesAndValidation(TestSupport::Context &context)
{
    static_assert(std::is_nothrow_default_constructible_v<Desktop::Cursor>);
    static_assert(std::is_nothrow_copy_constructible_v<Desktop::Cursor>);
    static_assert(std::is_nothrow_copy_assignable_v<Desktop::Cursor>);
    static_assert(std::is_nothrow_move_constructible_v<Desktop::Cursor>);
    static_assert(std::is_nothrow_move_assignable_v<Desktop::Cursor>);
    static_assert(std::is_nothrow_destructible_v<Desktop::Cursor>);
    static_assert(noexcept(Desktop::createCursor(std::span<const Desktop::Types::Cursor::ImageView>{})));
    static_assert(noexcept(Desktop::createCursor(std::declval<const Desktop::Types::Cursor::ImageView &>())));
    static_assert(noexcept(Desktop::setCursor(std::declval<Desktop::Window &>(), std::declval<const Desktop::Cursor &>())));
    static_assert(noexcept(Desktop::hasCustomCursor(std::declval<const Desktop::Window &>())));

    Desktop::Cursor empty;
    static_cast<void>(context.expectFalse("default Cursor is invalid", empty.isValid()));

    const auto expectInvalid = [&context](std::string_view name, const Desktop::Types::Cursor::ImageView &image)
    {
        const auto result = Desktop::createCursor(std::span{&image, std::size_t{1}});
        static_cast<void>(context.expectEq(name, ErrorCode::InvalidArgument, result.status.code));
        static_cast<void>(context.expectFalse("invalid input does not publish a Cursor", result.cursor.isValid()));
    };

    static_cast<void>(context.expectTrue(
        "cursor hotspot coordinates compare structurally",
        Desktop::Types::Cursor::PixelPosition{1, 2} == Desktop::Types::Cursor::PixelPosition{1, 2}));

    const auto noVariants = Desktop::createCursor(std::span<const Desktop::Types::Cursor::ImageView>{});
    static_cast<void>(context.expectEq("zero cursor variants are invalid", ErrorCode::InvalidArgument, noVariants.status.code));

    const CursorTest::Pixels packedPixels = CursorTest::pixels();
    Desktop::Types::Cursor::ImageView candidate = CursorTest::image(packedPixels);
    candidate.size.width = 0;
    expectInvalid("zero cursor width is invalid", candidate);
    candidate = CursorTest::image(packedPixels);
    candidate.size.height = 0;
    expectInvalid("zero cursor height is invalid", candidate);
    candidate = CursorTest::image(packedPixels);
    candidate.intendedDpi = 0;
    expectInvalid("zero intended DPI is invalid", candidate);
    candidate = CursorTest::image(packedPixels);
    candidate.hotspot.x = candidate.size.width;
    expectInvalid("cursor hotspot x must be in bounds", candidate);
    candidate = CursorTest::image(packedPixels);
    candidate.hotspot.y = candidate.size.height;
    expectInvalid("cursor hotspot y must be in bounds", candidate);
    candidate = CursorTest::image(packedPixels);
    candidate.rowStrideBytes = 7;
    expectInvalid("cursor stride shorter than packed row is invalid", candidate);
    candidate = CursorTest::image(packedPixels);
    candidate.rowStrideBytes = std::numeric_limits<std::size_t>::max();
    expectInvalid("cursor total payload multiplication overflow is invalid", candidate);
    candidate = CursorTest::image(std::span{packedPixels}.first(15));
    expectInvalid("undersized cursor payload is invalid", candidate);
    std::array<std::byte, 17> oversized{};
    candidate = CursorTest::image(oversized);
    expectInvalid("oversized cursor payload is invalid", candidate);
    candidate = CursorTest::image(packedPixels);
    candidate.size.width = std::numeric_limits<std::uint32_t>::max();
    expectInvalid("cursor row size outside native range is invalid", candidate);

    std::array duplicateDpi{CursorTest::image(packedPixels, 144), CursorTest::image(packedPixels, 144)};
    const auto duplicate = Desktop::createCursor(duplicateDpi);
    static_cast<void>(context.expectEq("duplicate cursor DPI is invalid", ErrorCode::InvalidArgument, duplicate.status.code));
    static_cast<void>(context.expectFalse("duplicate DPI does not publish a Cursor", duplicate.cursor.isValid()));

    auto packed = CursorTest::create(packedPixels);
    static_cast<void>(context.expectTrue("single-image cursor overload creates successfully", packed.status.ok()));
    static_cast<void>(context.expectTrue("successful creation publishes a valid Cursor", packed.cursor.isValid()));

    std::array<std::byte, 24> paddedPixels{};
    const std::span packedRows{packedPixels};
    const std::span paddedRows{paddedPixels};
    std::ranges::copy(packedRows.first(8), paddedRows.first(8).begin());
    std::ranges::copy(packedRows.subspan(8), paddedRows.subspan(12, 8).begin());
    const Desktop::Types::Cursor::ImageView paddedImage = CursorTest::image(paddedPixels, 96, 12);
    auto padded = Desktop::createCursor(std::span{&paddedImage, std::size_t{1}});
    static_cast<void>(context.expectTrue("padded cursor rows create successfully", padded.status.ok()));

    Desktop::Cursor copy = packed.cursor;
    static_cast<void>(context.expectTrue("copied Cursor remains valid", copy.isValid()));
    Desktop::Cursor assigned;
    assigned = copy;
    static_cast<void>(context.expectTrue("copy-assigned Cursor remains valid", assigned.isValid()));
    Desktop::Cursor moved = std::move(copy);
    static_cast<void>(context.expectTrue("moved Cursor remains valid", moved.isValid()));
    Desktop::Cursor moveAssigned;
    moveAssigned = std::move(assigned);
    static_cast<void>(context.expectTrue("move-assigned Cursor remains valid", moveAssigned.isValid()));
}

#if DESKTOP_INTERNAL_TEST_HOOKS
void testCursorNativeResources(TestSupport::Context &context)
{
    Desktop::TestHooks::resetFailures();
    const CursorTest::Pixels pixels = CursorTest::pixels();
    const std::size_t createdBefore = Desktop::TestHooks::createdCustomCursorCount();
    const std::size_t destroyedBefore = Desktop::TestHooks::destroyedCustomCursorCount();
    {
        auto one = CursorTest::create(pixels);
        static_cast<void>(context.expectTrue("one native cursor variant creates", one.status.ok()));
        static_cast<void>(
            context.expectEq("one native variant is retained", std::size_t{1}, Desktop::TestHooks::customCursorVariantCount(one.cursor)));
        const auto snapshot = Desktop::TestHooks::inspectCustomCursorVariant(one.cursor, 0);
        static_cast<void>(context.expectTrue("native cursor variant can be inspected", snapshot.valid));
        static_cast<void>(context.expectEq("custom cursor hotspot x reaches Win32", std::uint32_t{1}, snapshot.hotspot.x));
        static_cast<void>(context.expectEq("custom cursor hotspot y reaches Win32", std::uint32_t{1}, snapshot.hotspot.y));
        static_cast<void>(context.expectEq(
            "RGBA cursor pixels convert to BGRA",
            std::array<std::byte, 4>{std::byte{0x33}, std::byte{0x22}, std::byte{0x11}, std::byte{0x44}},
            snapshot.firstBgraPixel));

        Desktop::Cursor shared = one.cursor;
        static_cast<void>(context.expectEq(
            "copying a Cursor does not duplicate native variants",
            createdBefore + 1,
            Desktop::TestHooks::createdCustomCursorCount()));
        static_cast<void>(shared.isValid());
    }
    static_cast<void>(
        context.expectEq("last Cursor reference destroys its native variant", destroyedBefore + 1, Desktop::TestHooks::destroyedCustomCursorCount()));

    std::array images{CursorTest::image(pixels, 96), CursorTest::image(pixels, 144), CursorTest::image(pixels, 192)};
    auto multiple = Desktop::createCursor(images);
    static_cast<void>(context.expectTrue("multiple native cursor variants create", multiple.status.ok()));
    static_cast<void>(
        context.expectEq("all native variants are eagerly retained", std::size_t{3}, Desktop::TestHooks::customCursorVariantCount(multiple.cursor)));

    const std::size_t partialCreated = Desktop::TestHooks::createdCustomCursorCount();
    const std::size_t partialDestroyed = Desktop::TestHooks::destroyedCustomCursorCount();
    Desktop::TestHooks::failCursorNativeCreationAfter(1);
    auto partial = Desktop::createCursor(images);
    static_cast<void>(context.expectEq("injected native cursor failure is translated", ErrorCode::NativeFailure, partial.status.code));
    static_cast<void>(context.expectFalse("partial native creation publishes no Cursor", partial.cursor.isValid()));
    static_cast<void>(
        context.expectEq("partial native creation made exactly one candidate", partialCreated + 1, Desktop::TestHooks::createdCustomCursorCount()));
    static_cast<void>(context.expectEq(
        "partial native creation cleans its earlier candidate",
        partialDestroyed + 1,
        Desktop::TestHooks::destroyedCustomCursorCount()));

    const std::size_t allocationCreated = Desktop::TestHooks::createdCustomCursorCount();
    Desktop::TestHooks::failNext(Desktop::TestHooks::FailurePoint::Allocation);
    auto allocation = CursorTest::create(pixels);
    static_cast<void>(context.expectEq("cursor allocation failure is translated", ErrorCode::OutOfMemory, allocation.status.code));
    static_cast<void>(context.expectFalse("cursor allocation failure publishes no Cursor", allocation.cursor.isValid()));
    static_cast<void>(
        context.expectEq("allocation failure creates no native candidate", allocationCreated, Desktop::TestHooks::createdCustomCursorCount()));

    const std::size_t stateAllocationCreated = Desktop::TestHooks::createdCustomCursorCount();
    const std::size_t stateAllocationDestroyed = Desktop::TestHooks::destroyedCustomCursorCount();
    Desktop::TestHooks::failNext(Desktop::TestHooks::FailurePoint::CursorStateAllocation);
    auto stateAllocation = Desktop::createCursor(images);
    static_cast<void>(context.expectEq("CursorState allocation failure is translated", ErrorCode::OutOfMemory, stateAllocation.status.code));
    static_cast<void>(context.expectFalse("CursorState allocation failure publishes no Cursor", stateAllocation.cursor.isValid()));
    static_cast<void>(context.expectEq(
        "CursorState allocation failure materializes all candidates first",
        stateAllocationCreated + images.size(),
        Desktop::TestHooks::createdCustomCursorCount()));
    static_cast<void>(context.expectEq(
        "CursorState allocation failure cleans all native candidates",
        stateAllocationDestroyed + images.size(),
        Desktop::TestHooks::destroyedCustomCursorCount()));
    Desktop::TestHooks::resetFailures();
}

void testCursorWindowIntegration(TestSupport::Context &context)
{
    const CursorTest::Pixels pixels = CursorTest::pixels();
    auto first = CursorTest::create(pixels, 96);
    auto second = CursorTest::create(pixels, 144);
    static_cast<void>(context.expectTrue("cursor integration resources create", first.status.ok() && second.status.ok()));
    if (!first.cursor.isValid() || !second.cursor.isValid())
        return;

    Desktop::Window closed;
    static_cast<void>(
        context.expectEq("setting a Cursor on a closed Window reports NotOpen", ErrorCode::NotOpen, Desktop::setCursor(closed, first.cursor).code));
    static_cast<void>(context.expectFalse("closed Window has no custom Cursor", Desktop::hasCustomCursor(closed)));

    Desktop::Types::Description description;
    description.title = "GameWIP custom cursor integration";
    description.visible = false;
    Desktop::Window window;
    static_cast<void>(context.expectTrue("custom cursor fixture opens", window.open(description, 8).ok()));
    if (!window.isOpen())
        return;

    Desktop::Cursor invalid;
    static_cast<void>(context.expectEq("invalid Cursor selection is rejected", ErrorCode::InvalidArgument, Desktop::setCursor(window, invalid).code));
    static_cast<void>(context.expectTrue("first custom Cursor selection succeeds", Desktop::setCursor(window, first.cursor).ok()));
    static_cast<void>(context.expectTrue("custom Cursor binding is reported", Desktop::hasCustomCursor(window)));
    static_cast<void>(
        context.expectEq("custom Cursor leaves cached fallback shape unchanged", Desktop::Types::CursorShape::Arrow, window.cursorShape()));
    static_cast<void>(context.expectTrue("custom-to-custom replacement succeeds", Desktop::setCursor(window, second.cursor).ok()));
    static_cast<void>(
        context.expectEq("replacement selects its intended DPI", std::uint32_t{144}, Desktop::TestHooks::customCursorBindingDpi(window)));

    Desktop::Window sharedWindow;
    static_cast<void>(context.expectTrue("second custom cursor fixture opens", sharedWindow.open(description, 8).ok()));
    const std::size_t createdBeforeSharing = Desktop::TestHooks::createdCustomCursorCount();
    static_cast<void>(context.expectTrue("one Cursor can bind a second Window", Desktop::setCursor(sharedWindow, second.cursor).ok()));
    static_cast<void>(
        context.expectTrue("both Windows retain the same custom Cursor", Desktop::hasCustomCursor(window) && Desktop::hasCustomCursor(sharedWindow)));
    static_cast<void>(context.expectEq(
        "sharing a Cursor across Windows creates no native variant",
        createdBeforeSharing,
        Desktop::TestHooks::createdCustomCursorCount()));

    Desktop::TestHooks::failNext(Desktop::TestHooks::FailurePoint::Allocation);
    static_cast<void>(
        context.expectEq("binding allocation failure is translated", ErrorCode::OutOfMemory, Desktop::setCursor(window, first.cursor).code));
    static_cast<void>(context.expectEq(
        "binding allocation failure preserves previous custom selection",
        std::uint32_t{144},
        Desktop::TestHooks::customCursorBindingDpi(window)));

    Desktop::TestHooks::failNext(Desktop::TestHooks::FailurePoint::CursorBinding);
    static_cast<void>(context.expectEq("binding failure is transactional", ErrorCode::NativeFailure, Desktop::setCursor(window, first.cursor).code));
    static_cast<void>(context.expectEq(
        "failed binding preserves previous custom selection",
        std::uint32_t{144},
        Desktop::TestHooks::customCursorBindingDpi(window)));

    std::atomic<ErrorCode> wrongThreadCode{ErrorCode::Success};
    std::thread wrongThread(
        [&]
        {
            wrongThreadCode.store(Desktop::setCursor(window, first.cursor).code, std::memory_order_relaxed);
        });
    wrongThread.join();
    static_cast<void>(context.expectEq("wrong-thread Cursor selection reports ResourceBusy", ErrorCode::ResourceBusy, wrongThreadCode.load()));

    std::atomic<bool> wrongThreadHasCustomCursor{true};
    std::thread wrongThreadQuery(
        [&]
        {
            wrongThreadHasCustomCursor.store(Desktop::hasCustomCursor(window), std::memory_order_relaxed);
        });
    wrongThreadQuery.join();
    static_cast<void>(context.expectFalse("wrong-thread custom Cursor query reports false", wrongThreadHasCustomCursor.load()));

    Desktop::TestHooks::failNext(Desktop::TestHooks::FailurePoint::SystemCursorLoad);
    static_cast<void>(context.expectEq(
        "system cursor load failure is transactional",
        ErrorCode::NativeFailure,
        window.setCursorShape(Desktop::Types::CursorShape::Hand).code));
    static_cast<void>(context.expectTrue("failed system fallback preserves custom binding", Desktop::hasCustomCursor(window)));
    static_cast<void>(context.expectEq("failed system fallback preserves cached shape", Desktop::Types::CursorShape::Arrow, window.cursorShape()));

    Desktop::TestHooks::failNext(Desktop::TestHooks::FailurePoint::CursorBinding);
    static_cast<void>(context.expectEq(
        "custom binding removal failure is transactional",
        ErrorCode::NativeFailure,
        window.setCursorShape(Desktop::Types::CursorShape::Hand).code));
    static_cast<void>(context.expectTrue("failed custom binding removal preserves custom binding", Desktop::hasCustomCursor(window)));
    static_cast<void>(
        context.expectEq("failed custom binding removal preserves cached shape", Desktop::Types::CursorShape::Arrow, window.cursorShape()));

    static_cast<void>(
        context.expectTrue("system cursor shape replaces custom Cursor", window.setCursorShape(Desktop::Types::CursorShape::Hand).ok()));
    static_cast<void>(context.expectFalse("system cursor shape clears custom binding", Desktop::hasCustomCursor(window)));
    static_cast<void>(context.expectEq("system cursor replacement updates cached shape", Desktop::Types::CursorShape::Hand, window.cursorShape()));

    static_cast<void>(context.expectTrue("custom Cursor can be selected again", Desktop::setCursor(window, first.cursor).ok()));
    first.cursor = {};
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
    static_cast<void>(context.expectTrue("closed Window reopens", window.open(description, 8).ok()));
    static_cast<void>(context.expectFalse("reopened Window has no stale custom binding", Desktop::hasCustomCursor(window)));
    static_cast<void>(context.expectTrue("reopened custom cursor fixture closes", window.close().ok()));
}

void testCursorLifecycle(TestSupport::Context &context)
{
    const CursorTest::Pixels pixels = CursorTest::pixels();
    std::array images{CursorTest::image(pixels, 96), CursorTest::image(pixels, 144)};
    auto cursor = Desktop::createCursor(images);
    static_cast<void>(context.expectTrue("DPI cursor resource creates", cursor.status.ok()));
    if (!cursor.cursor.isValid())
        return;

    Desktop::Types::Description description;
    description.title = "GameWIP custom cursor lifecycle";
    description.visible = false;
    Desktop::Window window;
    static_cast<void>(context.expectTrue("DPI cursor fixture opens", window.open(description, 8).ok()));
    static_cast<void>(context.expectTrue("DPI cursor binds", Desktop::setCursor(window, cursor.cursor).ok()));
    const auto native = Desktop::Native::Win32::getHandle(window);
    static_cast<void>(context.expectTrue("DPI cursor fixture exposes its native handle", native.status.ok()));
    if (!native.status.ok())
        return;
    RECT suggested{};
    static_cast<void>(GetWindowRect(native.handle.window, &suggested));
    static_cast<void>(SendMessageW(native.handle.window, WM_DPICHANGED, MAKEWPARAM(120, 120), reinterpret_cast<LPARAM>(&suggested)));
    static_cast<void>(context.expectEq(
        "DPI transition tie chooses higher prebuilt variant",
        std::uint32_t{144},
        Desktop::TestHooks::customCursorBindingDpi(window)));
    static_cast<void>(SendMessageW(native.handle.window, WM_DPICHANGED, MAKEWPARAM(100, 100), reinterpret_cast<LPARAM>(&suggested)));
    static_cast<void>(
        context.expectEq("DPI transition chooses nearest prebuilt variant", std::uint32_t{96}, Desktop::TestHooks::customCursorBindingDpi(window)));
    const std::size_t createdBeforeReselect = Desktop::TestHooks::createdCustomCursorCount();
    static_cast<void>(SendMessageW(native.handle.window, WM_DPICHANGED, MAKEWPARAM(144, 144), reinterpret_cast<LPARAM>(&suggested)));
    static_cast<void>(
        context.expectEq("DPI transition chooses exact prebuilt variant", std::uint32_t{144}, Desktop::TestHooks::customCursorBindingDpi(window)));
    static_cast<void>(
        context.expectEq("DPI reselection creates no native resource", createdBeforeReselect, Desktop::TestHooks::createdCustomCursorCount()));
    const std::size_t destroyedBeforeNormal = Desktop::TestHooks::destroyedCustomCursorCount();
    cursor.cursor = {};
    static_cast<void>(context.expectTrue("DPI cursor fixture closes", window.close().ok()));
    static_cast<void>(context.expectEq(
        "normal close releases the final custom cursor binding",
        destroyedBeforeNormal + images.size(),
        Desktop::TestHooks::destroyedCustomCursorCount()));

    const std::size_t destroyedBeforeUnexpected = Desktop::TestHooks::destroyedCustomCursorCount();
    auto unexpectedCursor = CursorTest::create(pixels);
    Desktop::Window unexpected;
    static_cast<void>(context.expectTrue("unexpected-destruction cursor fixture opens", unexpected.open(description, 4).ok()));
    static_cast<void>(context.expectTrue("unexpected-destruction cursor binds", Desktop::setCursor(unexpected, unexpectedCursor.cursor).ok()));
    unexpectedCursor.cursor = {};
    static_cast<void>(context.expectTrue("native Window destruction succeeds", Desktop::TestHooks::destroyNativeWindow(unexpected).ok()));
    static_cast<void>(context.expectEq(
        "unexpected native destruction releases custom cursor binding",
        destroyedBeforeUnexpected + 1,
        Desktop::TestHooks::destroyedCustomCursorCount()));
    static_cast<void>(context.expectTrue("unexpected native destruction finalizes", unexpected.close().ok()));

    const std::size_t destroyedBeforeDeferred = Desktop::TestHooks::destroyedCustomCursorCount();
    auto deferredCursor = CursorTest::create(pixels);
    auto deferred = std::make_unique<Desktop::Window>();
    static_cast<void>(context.expectTrue("deferred cursor fixture opens", deferred->open(description, 4).ok()));
    static_cast<void>(context.expectTrue("deferred cursor binds", Desktop::setCursor(*deferred, deferredCursor.cursor).ok()));
    deferredCursor.cursor = {};
    std::thread destroyer(
        [owned = std::move(deferred)]() mutable
        {
            owned.reset();
        });
    destroyer.join();
    static_cast<void>(context.expectTrue("owner pump drains deferred cursor cleanup", Desktop::Events::poll().status.ok()));
    static_cast<void>(context.expectEq(
        "deferred cleanup releases custom cursor binding",
        destroyedBeforeDeferred + 1,
        Desktop::TestHooks::destroyedCustomCursorCount()));
}
#endif
