/// @file desktop_clipboard_contract_tests.inl
/// @brief Clipboard value and input validation tests.

/// ------------------------------------------------------------
/// Clipboard values and validation
/// ------------------------------------------------------------

void testClipboardValuesAndValidation(TestSupport::Context &context)
{
    static_assert(std::is_same_v<decltype(Transfer::FormatView::text().customName()), std::string_view>);
    static_assert(std::is_same_v<decltype(Transfer::Format{}.customName), std::string>);
    static_assert(noexcept(Clipboard::hasFormat(Transfer::FormatView::text())));
    static_assert(noexcept(Clipboard::hasFormat(Transfer::FormatView::text(), Clipboard::kNoWait)));
    static_assert(noexcept(Clipboard::getFormats()));
    static_assert(noexcept(Clipboard::readText()));
    static_assert(noexcept(Clipboard::readFiles()));
    static_assert(noexcept(Clipboard::readImage()));
    static_assert(noexcept(Clipboard::readCustomData(std::declval<std::string_view>())));
    static_assert(noexcept(Clipboard::clear()));

    // Validate timeout handling and registered format names.
    const auto negative = std::chrono::milliseconds{-1};
    static_cast<void>(context.expectEq(
        "negative hasFormat timeout is invalid",
        StatusCode::InvalidArgument,
        Clipboard::hasFormat(Transfer::FormatView::text(), negative).status().code()));
    static_cast<void>(
        context.expectEq("negative getFormats timeout is invalid", StatusCode::InvalidArgument, Clipboard::getFormats(negative).status().code()));
    static_cast<void>(
        context.expectEq("negative read timeout is invalid", StatusCode::InvalidArgument, Clipboard::readText(negative).status().code()));
    static_cast<void>(context.expectEq("negative clear timeout is invalid", StatusCode::InvalidArgument, Clipboard::clear(negative).status().code()));
    const auto noWaitProbe = [](Transfer::FormatView format)
    {
        for(int attempt = 0; attempt < 3; ++attempt)
        {
            auto result = Clipboard::hasFormat(format, Clipboard::kNoWait);

            if(result.status().code() != StatusCode::ResourceBusy || attempt == 2)
            {
                return result;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds{50});
        }

        return Clipboard::hasFormat(format, Clipboard::kNoWait);
    };
    const auto namedText = noWaitProbe(Transfer::FormatView::text());

    if(namedText.status().code() == StatusCode::ResourceBusy)
    {
        context.skip("Clipboard format validation", "Windows Clipboard remained busy after three no-wait attempts");
        return;
    }
    static_cast<void>(context.expectEq("named text format is valid", StatusCode::Success, namedText.status().code()));
    const auto namedCustom = noWaitProbe(Transfer::FormatView::custom("wrong"));

    if(namedCustom.status().code() == StatusCode::ResourceBusy)
    {
        context.skip("Clipboard format validation", "Windows Clipboard remained busy after three no-wait attempts");
        return;
    }
    static_cast<void>(context.expectEq("named custom format is valid", StatusCode::Success, namedCustom.status().code()));
    static_cast<void>(context.expectEq(
        "custom format requires name",
        StatusCode::InvalidArgument,
        Clipboard::hasFormat(Transfer::FormatView::custom(std::string_view{}), Clipboard::kNoWait).status().code()));
    static_cast<void>(context.expectEq(
        "custom format rejects malformed UTF-8",
        StatusCode::InvalidArgument,
        Clipboard::hasFormat(Transfer::FormatView::custom(std::string_view{"\xC3", 1}), Clipboard::kNoWait).status().code()));
    static_cast<void>(context.expectEq(
        "custom format rejects embedded null",
        StatusCode::InvalidArgument,
        Clipboard::hasFormat(Transfer::FormatView::custom(std::string_view{"a\0b", 3}), Clipboard::kNoWait).status().code()));

    // Reject invalid transfer payloads before they change the Clipboard.
    const std::span<const Transfer::ItemView> emptyItems;
    const auto emptyWrite = Clipboard::write(emptyItems, Clipboard::kNoWait);
    static_cast<void>(context.expectEq("empty multi-format write is invalid", StatusCode::InvalidArgument, emptyWrite.status().code()));
    if(context.expectTrue("invalid write retains progress", emptyWrite.hasValue()))
    {
        static_cast<void>(context.expectEq("invalid write never starts", ClipboardTypes::CommitState::NotStarted, emptyWrite.value().commitState));
        static_cast<void>(context.expectEq("invalid write publishes nothing", std::size_t{0}, emptyWrite.value().formatsPublished));
    }

    static_cast<void>(context.expectEq(
        "malformed text is invalid",
        StatusCode::InvalidArgument,
        Clipboard::writeText(std::string_view{"\xF0\x28\x8C\x28", 4}, Clipboard::kNoWait).status().code()));
    static_cast<void>(context.expectEq(
        "embedded text null is invalid at Win32 boundary",
        StatusCode::InvalidArgument,
        Clipboard::writeText(std::string_view{"a\0b", 3}, Clipboard::kNoWait).status().code()));
    static_cast<void>(
        context.expectEq("empty file list is invalid", StatusCode::InvalidArgument, Clipboard::writeFiles({}, Clipboard::kNoWait).status().code()));
    const std::array relativePaths{GameWIP::FileSystem::Types::Path{"relative.txt"}};
    static_cast<void>(context.expectEq(
        "relative file path is invalid",
        StatusCode::InvalidArgument,
        Clipboard::writeFiles(relativePaths, Clipboard::kNoWait).status().code()));

    const std::array<std::byte, 4> onePixel{};
    static_cast<void>(context.expectEq(
        "zero-width image is invalid",
        StatusCode::InvalidArgument,
        Clipboard::writeImage({{0, 1}, 0, onePixel}, Clipboard::kNoWait).status().code()));
    static_cast<void>(context.expectEq(
        "undersized image stride is invalid",
        StatusCode::InvalidArgument,
        Clipboard::writeImage({{1, 1}, 3, onePixel}, Clipboard::kNoWait).status().code()));
    static_cast<void>(context.expectEq(
        "mismatched image extent is invalid",
        StatusCode::InvalidArgument,
        Clipboard::writeImage({{2, 1}, 0, onePixel}, Clipboard::kNoWait).status().code()));
#if DESKTOP_INTERNAL_TEST_HOOKS
    // Verify deadline saturation without waiting.
    Desktop::Internal::TestHooks::failNext(Desktop::Internal::TestHooks::FailurePoint::ClipboardAccess);
    static_cast<void>(context.expectEq(
        "maximum positive timeout saturates its deadline without waiting",
        StatusCode::ResourceBusy,
        Clipboard::getFormats(std::chrono::milliseconds::max()).status().code()));
#endif
}
