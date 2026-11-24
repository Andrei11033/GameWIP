/// @file desktop_cursor_selection_tests.inl
/// @brief Cursor value, validation, and DPI-selection tests.

/// ------------------------------------------------------------
/// Cursor values and selection
/// ------------------------------------------------------------

void testCursorDpiSelection(TestSupport::Context &context)
{
    constexpr std::array<std::uint32_t, 3> orderedDpis{96, 144, 192};

    static_cast<void>(
        context.expectEq("cursor DPI selection chooses exact match", std::size_t{1}, Desktop::Internal::selectDpiVariantIndex(orderedDpis, 144)));

    static_cast<void>(context.expectEq(
        "cursor DPI selection clamps below supplied range",
        std::size_t{0},
        Desktop::Internal::selectDpiVariantIndex(orderedDpis, 72)));

    static_cast<void>(context.expectEq(
        "cursor DPI selection clamps above supplied range",
        std::size_t{2},
        Desktop::Internal::selectDpiVariantIndex(orderedDpis, 240)));

    static_cast<void>(context.expectEq(
        "cursor DPI selection chooses nearest lower variant",
        std::size_t{0},
        Desktop::Internal::selectDpiVariantIndex(orderedDpis, 110)));

    static_cast<void>(context.expectEq(
        "cursor DPI selection chooses nearest higher variant",
        std::size_t{2},
        Desktop::Internal::selectDpiVariantIndex(orderedDpis, 170)));

    static_cast<void>(context.expectEq(
        "cursor DPI selection chooses higher variant on equal distance",
        std::size_t{1},
        Desktop::Internal::selectDpiVariantIndex(orderedDpis, 120)));

    constexpr std::array<std::uint32_t, 3> unorderedDpis{192, 96, 144};

    static_cast<void>(context.expectEq(
        "cursor DPI selection does not require sorted variants",
        std::size_t{2},
        Desktop::Internal::selectDpiVariantIndex(unorderedDpis, 120)));
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

    const Desktop::Cursor empty;
    static_cast<void>(context.expectFalse("default Cursor is invalid", empty.isValid()));

    const auto expectInvalid = [&context](std::string_view name, const Desktop::Types::Cursor::ImageView &image)
    {
        const auto result = Desktop::createCursor(std::span{&image, std::size_t{1}});
        static_cast<void>(context.expectEq(name, ErrorCode::InvalidArgument, result.status.code));
        static_cast<void>(context.expectFalse("invalid input does not publish a Cursor", result.value.has_value() && result->isValid()));
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
    static_cast<void>(context.expectFalse("duplicate DPI does not publish a Cursor", duplicate.value.has_value() && duplicate->isValid()));

    // Verify successful creation and value semantics.
    auto packed = CursorTest::create(packedPixels);
    if(!context.expectTrue("single-image cursor overload creates successfully", packed.status.ok()))
    {
        return;
    }

    static_cast<void>(context.expectTrue("successful creation publishes a valid Cursor", packed->isValid()));

    std::array<std::byte, 24> paddedPixels{};
    const std::span packedRows{packedPixels};
    const std::span paddedRows{paddedPixels};
    std::ranges::copy(packedRows.first(8), paddedRows.first(8).begin());
    std::ranges::copy(packedRows.subspan(8), paddedRows.subspan(12, 8).begin());
    const Desktop::Types::Cursor::ImageView paddedImage = CursorTest::image(paddedPixels, 96, 12);
    const auto padded = Desktop::createCursor(std::span{&paddedImage, std::size_t{1}});
    if(!context.expectTrue("padded cursor rows create successfully", padded.status.ok()))
    {
        return;
    }

    Desktop::Cursor copy = *packed;
    static_cast<void>(context.expectTrue("copied Cursor remains valid", copy.isValid()));
    Desktop::Cursor assigned;
    assigned = copy;
    static_cast<void>(context.expectTrue("copy-assigned Cursor remains valid", assigned.isValid()));
    const Desktop::Cursor moved = std::move(copy);
    static_cast<void>(context.expectTrue("moved Cursor remains valid", moved.isValid()));
    Desktop::Cursor moveAssigned;
    moveAssigned = std::move(assigned);
    static_cast<void>(context.expectTrue("move-assigned Cursor remains valid", moveAssigned.isValid()));
}
