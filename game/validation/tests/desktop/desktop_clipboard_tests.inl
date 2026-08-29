/// @file desktop_clipboard_tests.inl
/// @brief Clipboard public contract, native round-trip, transaction, and failure validation.

namespace Clipboard = Desktop::Clipboard;
namespace ClipboardTypes = Desktop::Types::Clipboard;
namespace Transfer = Desktop::Types::DataTransfer;

#if defined(__clang__)
#pragma clang unsafe_buffer_usage begin
#endif
std::span<std::byte> nativeClipboardBytes(void *data, std::size_t size) noexcept
{
    return {static_cast<std::byte *>(data), size};
}
#if defined(__clang__)
#pragma clang unsafe_buffer_usage end
#endif

bool publishNativeClipboardData(UINT format, std::span<const std::byte> bytes)
{
    HWND owner =
        CreateWindowExW(0, L"STATIC", L"GameWIP Clipboard Test Owner", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (owner == nullptr || OpenClipboard(owner) == FALSE)
    {
        if (owner != nullptr)
            static_cast<void>(DestroyWindow(owner));
        return false;
    }
    bool success = EmptyClipboard() != FALSE;
    HGLOBAL memory = success ? GlobalAlloc(GMEM_MOVEABLE, bytes.size()) : nullptr;
    success = success && memory != nullptr;
    if (success && !bytes.empty())
    {
        void *destination = GlobalLock(memory);
        success = destination != nullptr;
        if (success)
            std::ranges::copy(bytes, nativeClipboardBytes(destination, bytes.size()).begin());
        if (destination != nullptr)
            static_cast<void>(GlobalUnlock(memory));
    }
    if (success)
    {
        success = SetClipboardData(format, memory) != nullptr;
        if (success)
            memory = nullptr;
    }
    if (memory != nullptr)
        static_cast<void>(GlobalFree(memory));
    success = CloseClipboard() != FALSE && success;
    static_cast<void>(DestroyWindow(owner));
    return success;
}

void testClipboardValuesAndValidation(TestSupport::Context &context)
{
    static_assert(std::is_same_v<decltype(Transfer::FormatView{}.customName), std::string_view>);
    static_assert(std::is_same_v<decltype(Transfer::Format{}.customName), std::string>);
    static_assert(noexcept(Clipboard::hasFormat(Transfer::FormatView{})));
    static_assert(noexcept(Clipboard::hasFormat(Transfer::FormatView{}, Clipboard::kNoWait)));
    static_assert(noexcept(Clipboard::getFormats()));
    static_assert(noexcept(Clipboard::readText()));
    static_assert(noexcept(Clipboard::readFiles()));
    static_assert(noexcept(Clipboard::readImage()));
    static_assert(noexcept(Clipboard::readCustomData(std::declval<std::string_view>())));
    static_assert(noexcept(Clipboard::clear()));

    const auto negative = std::chrono::milliseconds{-1};
    static_cast<void>(
        context.expectEq("negative hasFormat timeout is invalid", ErrorCode::InvalidArgument, Clipboard::hasFormat({}, negative).status.code));
    static_cast<void>(
        context.expectEq("negative getFormats timeout is invalid", ErrorCode::InvalidArgument, Clipboard::getFormats(negative).status.code));
    static_cast<void>(context.expectEq("negative read timeout is invalid", ErrorCode::InvalidArgument, Clipboard::readText(negative).status.code));
    static_cast<void>(context.expectEq("negative clear timeout is invalid", ErrorCode::InvalidArgument, Clipboard::clear(negative).status.code));
    static_cast<void>(context.expectEq(
        "unknown format kind is invalid",
        ErrorCode::InvalidArgument,
        Clipboard::hasFormat({static_cast<Transfer::FormatKind>(999), {}}, Clipboard::kNoWait).status.code));
    static_cast<void>(context.expectEq(
        "standard format rejects custom name",
        ErrorCode::InvalidArgument,
        Clipboard::hasFormat({Transfer::FormatKind::Text, "wrong"}, Clipboard::kNoWait).status.code));
    static_cast<void>(context.expectEq(
        "custom format requires name",
        ErrorCode::InvalidArgument,
        Clipboard::hasFormat({Transfer::FormatKind::Custom, {}}, Clipboard::kNoWait).status.code));
    static_cast<void>(context.expectEq(
        "custom format rejects malformed UTF-8",
        ErrorCode::InvalidArgument,
        Clipboard::hasFormat({Transfer::FormatKind::Custom, std::string_view{"\xC3", 1}}, Clipboard::kNoWait).status.code));
    static_cast<void>(context.expectEq(
        "custom format rejects embedded null",
        ErrorCode::InvalidArgument,
        Clipboard::hasFormat({Transfer::FormatKind::Custom, std::string_view{"a\0b", 3}}, Clipboard::kNoWait).status.code));

    std::span<const Transfer::ItemView> emptyItems;
    const auto emptyWrite = Clipboard::write(emptyItems, Clipboard::kNoWait);
    static_cast<void>(context.expectEq("empty multi-format write is invalid", ErrorCode::InvalidArgument, emptyWrite.status.code));
    static_cast<void>(context.expectEq("invalid write never starts", ClipboardTypes::CommitState::NotStarted, emptyWrite.commitState));
    static_cast<void>(context.expectEq("invalid write publishes nothing", std::size_t{0}, emptyWrite.formatsPublished));

    static_cast<void>(context.expectEq(
        "malformed text is invalid",
        ErrorCode::InvalidArgument,
        Clipboard::writeText(std::string_view{"\xF0\x28\x8C\x28", 4}, Clipboard::kNoWait).status.code));
    static_cast<void>(context.expectEq(
        "embedded text null is invalid at Win32 boundary",
        ErrorCode::InvalidArgument,
        Clipboard::writeText(std::string_view{"a\0b", 3}, Clipboard::kNoWait).status.code));
    static_cast<void>(
        context.expectEq("empty file list is invalid", ErrorCode::InvalidArgument, Clipboard::writeFiles({}, Clipboard::kNoWait).status.code));
    const std::array relativePaths{GameWIP::FileSystem::Types::Path{"relative.txt"}};
    static_cast<void>(context.expectEq(
        "relative file path is invalid",
        ErrorCode::InvalidArgument,
        Clipboard::writeFiles(relativePaths, Clipboard::kNoWait).status.code));

    const std::array<std::byte, 4> onePixel{};
    static_cast<void>(context.expectEq(
        "zero-width image is invalid",
        ErrorCode::InvalidArgument,
        Clipboard::writeImage({{0, 1}, 0, onePixel}, Clipboard::kNoWait).status.code));
    static_cast<void>(context.expectEq(
        "undersized image stride is invalid",
        ErrorCode::InvalidArgument,
        Clipboard::writeImage({{1, 1}, 3, onePixel}, Clipboard::kNoWait).status.code));
    static_cast<void>(context.expectEq(
        "mismatched image extent is invalid",
        ErrorCode::InvalidArgument,
        Clipboard::writeImage({{2, 1}, 0, onePixel}, Clipboard::kNoWait).status.code));
#if DESKTOP_INTERNAL_TEST_HOOKS
    Desktop::TestHooks::failNext(Desktop::TestHooks::FailurePoint::ClipboardAccess);
    static_cast<void>(context.expectEq(
        "maximum positive timeout saturates its deadline without waiting",
        ErrorCode::ResourceBusy,
        Clipboard::getFormats(std::chrono::milliseconds::max()).status.code));
#endif
}

void testClipboardRoundTrips(TestSupport::Context &context)
{
    const auto initialClear = Clipboard::clear();
    static_cast<void>(context.expectTrue("Clipboard clears without a GameWIP Window", initialClear.status.ok() && initialClear.cleared));
    const auto absentText = Clipboard::hasFormat({Transfer::FormatKind::Text, {}});
    static_cast<void>(context.expectTrue("absent predicate is successful false", absentText.status.ok() && !absentText.available));

    constexpr std::string_view text = "Clipboard UTF-8: caf\xC3\xA9 \xF0\x9F\x98\x80";
    const auto textWrite = Clipboard::writeText(text);
    static_cast<void>(context.expectTrue("UTF-8 text publishes", textWrite.status.ok()));
    static_cast<void>(context.expectEq("single text write publishes all", ClipboardTypes::CommitState::Published, textWrite.commitState));
    static_cast<void>(context.expectEq("single text count is one", std::size_t{1}, textWrite.formatsPublished));
    const auto textRead = Clipboard::readText();
    static_cast<void>(context.expectTrue("UTF-8 text reads", textRead.status.ok()));
    static_cast<void>(context.expectEq("UTF-8 and non-BMP text round trip", std::string{text}, textRead.text));
    static_cast<void>(context.expectTrue("text predicate is available", Clipboard::hasFormat({Transfer::FormatKind::Text, {}}).available));

    const auto emptyTextWrite = Clipboard::writeText("");
    static_cast<void>(context.expectTrue("empty text publishes", emptyTextWrite.status.ok()));
    const auto emptyTextRead = Clipboard::readText();
    static_cast<void>(context.expectTrue("empty text reads", emptyTextRead.status.ok()));
    static_cast<void>(context.expectTrue("empty text remains empty", emptyTextRead.text.empty()));

    const std::array paths{
        GameWIP::FileSystem::Types::Path{L"C:\\GameWIP Clipboard Nonexistent.txt"},
        GameWIP::FileSystem::Types::Path{L"C:\\Unicode-\u03A9-\u4E2D.dat"}};
    const auto filesWrite = Clipboard::writeFiles(paths);
    static_cast<void>(context.expectTrue("absolute nonexistent Unicode paths publish without IO", filesWrite.status.ok()));
    const auto filesRead = Clipboard::readFiles();
    static_cast<void>(context.expectTrue("file list reads", filesRead.status.ok()));
    static_cast<void>(context.expectEq("file list preserves order and spelling", std::vector(paths.begin(), paths.end()), filesRead.paths));

    const std::array<std::byte, 24> paddedPixels{
        std::byte{0x10}, std::byte{0x20}, std::byte{0x30}, std::byte{0x40}, std::byte{0x50}, std::byte{0x60}, std::byte{0x70}, std::byte{0x80},
        std::byte{0xCC}, std::byte{0xCC}, std::byte{0xCC}, std::byte{0xCC}, std::byte{0x90}, std::byte{0xA0}, std::byte{0xB0}, std::byte{0xC0},
        std::byte{0xD0}, std::byte{0xE0}, std::byte{0xF0}, std::byte{0xFF}, std::byte{0xDD}, std::byte{0xDD}, std::byte{0xDD}, std::byte{0xDD}};
    const auto imageWrite = Clipboard::writeImage({{2, 2}, 12, paddedPixels});
    static_cast<void>(context.expectTrue("padded RGBA8 image publishes", imageWrite.status.ok()));
    const auto imageRead = Clipboard::readImage();
    const std::vector<std::byte> expectedPixels{
        paddedPixels[0],
        paddedPixels[1],
        paddedPixels[2],
        paddedPixels[3],
        paddedPixels[4],
        paddedPixels[5],
        paddedPixels[6],
        paddedPixels[7],
        paddedPixels[12],
        paddedPixels[13],
        paddedPixels[14],
        paddedPixels[15],
        paddedPixels[16],
        paddedPixels[17],
        paddedPixels[18],
        paddedPixels[19]};
    static_cast<void>(context.expectTrue("DIBV5 image reads", imageRead.status.ok()));
    static_cast<void>(context.expectEq("image dimensions round trip", Desktop::Types::PixelSize{2, 2}, imageRead.image.size));
    static_cast<void>(context.expectEq("image read is tightly packed with alpha", expectedPixels, imageRead.image.rgba8));

    const std::array customBytes{std::byte{0x00}, std::byte{0x11}, std::byte{0xFE}, std::byte{0x7F}};
    const auto customWrite = Clipboard::writeCustomData({"MyCompany.Material", customBytes});
    static_cast<void>(context.expectTrue("arbitrary named binary format publishes", customWrite.status.ok()));
    const auto customRead = Clipboard::readCustomData("mycompany.material");
    static_cast<void>(context.expectTrue("custom names use case-insensitive Win32 identity", customRead.status.ok()));
    static_cast<void>(
        context.expectEq("opaque bytes including null round trip exactly", std::vector(customBytes.begin(), customBytes.end()), customRead.bytes));
    const auto formats = Clipboard::getFormats();
    static_cast<void>(context.expectTrue("custom format enumeration succeeds", formats.status.ok()));
    static_cast<void>(context.expectTrue(
        "enumeration owns the native custom name",
        std::ranges::find(formats.formats, Transfer::Format{Transfer::FormatKind::Custom, "MyCompany.Material"}) != formats.formats.end()));
    static_cast<void>(context.expectEq(
        "missing custom read is NotFound",
        ErrorCode::NotFound,
        Clipboard::readCustomData("GameWIP.Validation.DefinitelyMissing").status.code));

    const auto emptyCustomWrite = Clipboard::writeCustomData({"GameWIP.Validation.Empty", {}});
    static_cast<void>(
        context.expectEq("zero-byte custom publication reports Win32 limitation", ErrorCode::Unsupported, emptyCustomWrite.status.code));
    static_cast<void>(
        context.expectEq("zero-byte custom failure does not mutate", ClipboardTypes::CommitState::NotStarted, emptyCustomWrite.commitState));

    const std::array<wchar_t, 2> malformedWide{static_cast<wchar_t>(0xD800), L'\0'};
    const auto malformedWideBytes = std::as_bytes(std::span{malformedWide});
    static_cast<void>(
        context.expectTrue("native malformed Unicode fixture publishes", publishNativeClipboardData(CF_UNICODETEXT, malformedWideBytes)));
    static_cast<void>(context.expectEq("malformed native Unicode is EncodingFailed", ErrorCode::EncodingFailed, Clipboard::readText().status.code));

    BITMAPINFOHEADER dibHeader{};
    dibHeader.biSize = sizeof(BITMAPINFOHEADER);
    dibHeader.biWidth = 1;
    dibHeader.biHeight = 1;
    dibHeader.biPlanes = 1;
    dibHeader.biBitCount = 32;
    dibHeader.biCompression = BI_RGB;
    dibHeader.biSizeImage = 4;
    std::vector<std::byte> rgbDib(sizeof(dibHeader) + 4);
    std::ranges::copy(std::as_bytes(std::span{&dibHeader, std::size_t{1}}), rgbDib.begin());
    rgbDib[sizeof(dibHeader)] = std::byte{0x33};
    rgbDib[sizeof(dibHeader) + 1] = std::byte{0x22};
    rgbDib[sizeof(dibHeader) + 2] = std::byte{0x11};
    rgbDib[sizeof(dibHeader) + 3] = std::byte{0x00};
    static_cast<void>(context.expectTrue("foreign BI_RGB fixture publishes", publishNativeClipboardData(CF_DIB, rgbDib)));
    const auto rgbRead = Clipboard::readImage();
    static_cast<void>(context.expectTrue("foreign BI_RGB image reads", rgbRead.status.ok()));
    if (rgbRead.image.rgba8.size() == 4)
    {
        static_cast<void>(context.expectEq("foreign RGB red converts", std::byte{0x11}, rgbRead.image.rgba8[0]));
        static_cast<void>(context.expectEq("foreign RGB alpha becomes opaque", std::byte{0xFF}, rgbRead.image.rgba8[3]));
    }

    BITMAPV5HEADER unsupportedHeader{};
    unsupportedHeader.bV5Size = sizeof(BITMAPV5HEADER);
    unsupportedHeader.bV5Width = 1;
    unsupportedHeader.bV5Height = 1;
    unsupportedHeader.bV5Planes = 1;
    unsupportedHeader.bV5BitCount = 32;
    unsupportedHeader.bV5Compression = BI_BITFIELDS;
    unsupportedHeader.bV5SizeImage = 4;
    unsupportedHeader.bV5RedMask = 0x000000FFU;
    unsupportedHeader.bV5GreenMask = 0x0000FF00U;
    unsupportedHeader.bV5BlueMask = 0x00FF0000U;
    std::vector<std::byte> unsupportedDib(sizeof(unsupportedHeader) + 4);
    std::ranges::copy(std::as_bytes(std::span{&unsupportedHeader, std::size_t{1}}), unsupportedDib.begin());
    static_cast<void>(context.expectTrue("valid nonstandard-mask DIB fixture publishes", publishNativeClipboardData(CF_DIBV5, unsupportedDib)));
    static_cast<void>(context.expectEq("valid nonstandard-mask image is Unsupported", ErrorCode::Unsupported, Clipboard::readImage().status.code));
}

void testClipboardMultiFormatAndFailures(TestSupport::Context &context)
{
    const std::array firstBytes{std::byte{0x01}, std::byte{0x02}};
    const std::array secondBytes{std::byte{0x03}, std::byte{0x04}};
    const std::array<Transfer::ItemView, 3> items{
        Transfer::CustomView{"GameWIP.Validation.First", firstBytes},
        Transfer::TextView{"multi-format"},
        Transfer::CustomView{"GameWIP.Validation.Second", secondBytes}};
    const auto write = Clipboard::write(items);
    static_cast<void>(context.expectTrue("several custom formats plus text publish", write.status.ok()));
    static_cast<void>(context.expectEq("all multi-format items publish", items.size(), write.formatsPublished));
    const auto formats = Clipboard::getFormats();
    static_cast<void>(context.expectTrue("multi-format enumeration succeeds", formats.status.ok()));
    static_cast<void>(context.expectTrue("multi-format enumeration contains three portable entries", formats.formats.size() >= items.size()));
    if (formats.formats.size() >= items.size())
    {
        static_cast<void>(context.expectEq(
            "first caller format keeps priority",
            Transfer::Format{Transfer::FormatKind::Custom, "GameWIP.Validation.First"},
            formats.formats[0]));
        static_cast<void>(context.expectEq("text keeps caller priority", Transfer::Format{Transfer::FormatKind::Text, {}}, formats.formats[1]));
        static_cast<void>(context.expectEq(
            "second custom keeps caller priority",
            Transfer::Format{Transfer::FormatKind::Custom, "GameWIP.Validation.Second"},
            formats.formats[2]));
    }

    const std::array<Transfer::ItemView, 2> duplicateText{Transfer::TextView{"one"}, Transfer::TextView{"two"}};
    static_cast<void>(
        context.expectEq("duplicate standard format is rejected", ErrorCode::InvalidArgument, Clipboard::write(duplicateText).status.code));
    const std::array<Transfer::ItemView, 2> duplicateCustom{
        Transfer::CustomView{"GameWIP.Validation.Case", firstBytes},
        Transfer::CustomView{"gamewip.validation.case", secondBytes}};
    static_cast<void>(
        context.expectEq("duplicate native custom identity is rejected", ErrorCode::InvalidArgument, Clipboard::write(duplicateCustom).status.code));

    static_cast<void>(Clipboard::writeText("preserve me"));
    const auto invalidImage = Transfer::ImageView{{2, 2}, 0, firstBytes};
    const std::array<Transfer::ItemView, 2> invalidItems{Transfer::TextView{"replacement"}, invalidImage};
    const auto invalidWrite = Clipboard::write(invalidItems);
    static_cast<void>(context.expectEq("late preparation validation fails", ErrorCode::InvalidArgument, invalidWrite.status.code));
    static_cast<void>(context.expectEq("preparation failure remains NotStarted", ClipboardTypes::CommitState::NotStarted, invalidWrite.commitState));
    static_cast<void>(context.expectEq("preparation failure preserves old clipboard", std::string{"preserve me"}, Clipboard::readText().text));

#if DESKTOP_INTERNAL_TEST_HOOKS
    Desktop::TestHooks::resetFailures();
    Desktop::TestHooks::failNext(Desktop::TestHooks::FailurePoint::ClipboardAccess);
    static_cast<void>(context.expectEq("access busy is deterministic", ErrorCode::ResourceBusy, Clipboard::readText(Clipboard::kNoWait).status.code));
    Desktop::TestHooks::failNext(Desktop::TestHooks::FailurePoint::ClipboardAccess);
    static_cast<void>(context.expectEq("convenience overload shares access semantics", ErrorCode::ResourceBusy, Clipboard::readText().status.code));

    Desktop::TestHooks::failNext(Desktop::TestHooks::FailurePoint::ClipboardAllocation);
    const auto allocationFailure = Clipboard::writeText("allocation");
    static_cast<void>(context.expectEq("allocation failure translates", ErrorCode::OutOfMemory, allocationFailure.status.code));
    static_cast<void>(context.expectEq("allocation failure does not start", ClipboardTypes::CommitState::NotStarted, allocationFailure.commitState));

    Desktop::TestHooks::failNext(Desktop::TestHooks::FailurePoint::ClipboardOwnerCreation);
    static_cast<void>(context.expectEq("helper owner failure translates", ErrorCode::OpenFailed, Clipboard::writeText("owner").status.code));

    static_cast<void>(Clipboard::write(items));
    Desktop::TestHooks::failClipboardEnumerationAfter(1);
    const auto partialEnumeration = Clipboard::getFormats();
    static_cast<void>(context.expectEq("partial enumeration reports read failure", ErrorCode::ReadFailed, partialEnumeration.status.code));
    static_cast<void>(context.expectEq("partial enumeration preserves materialized prefix", std::size_t{1}, partialEnumeration.formats.size()));

    Desktop::TestHooks::failNext(Desktop::TestHooks::FailurePoint::ClipboardRead);
    static_cast<void>(context.expectEq("native read failure translates", ErrorCode::ReadFailed, Clipboard::readText().status.code));

    Desktop::TestHooks::failNext(Desktop::TestHooks::FailurePoint::ClipboardClear);
    const auto clearFailure = Clipboard::write(items);
    static_cast<void>(context.expectEq("native clear failure does not start", ClipboardTypes::CommitState::NotStarted, clearFailure.commitState));
    static_cast<void>(context.expectEq("native clear failure publishes zero", std::size_t{0}, clearFailure.formatsPublished));

    Desktop::TestHooks::failClipboardPublicationAt(0);
    const auto firstFailure = Clipboard::write(items);
    static_cast<void>(context.expectEq("first publication failure reports Cleared", ClipboardTypes::CommitState::Cleared, firstFailure.commitState));
    static_cast<void>(context.expectEq("first publication failure count is zero", std::size_t{0}, firstFailure.formatsPublished));

    Desktop::TestHooks::failClipboardPublicationAt(2);
    const auto partialFailure = Clipboard::write(items);
    static_cast<void>(
        context.expectEq("later publication failure is partial", ClipboardTypes::CommitState::PartiallyPublished, partialFailure.commitState));
    static_cast<void>(context.expectEq("partial publication reports exact prefix", std::size_t{2}, partialFailure.formatsPublished));

    Desktop::TestHooks::failNext(Desktop::TestHooks::FailurePoint::ClipboardClose);
    const auto closeAfterWrite = Clipboard::writeText("committed despite close failure");
    static_cast<void>(context.expectEq("close failure after full publication is reported", ErrorCode::CloseFailed, closeAfterWrite.status.code));
    static_cast<void>(context.expectEq("close failure preserves Published", ClipboardTypes::CommitState::Published, closeAfterWrite.commitState));
    static_cast<void>(context.expectEq("close failure preserves publication count", std::size_t{1}, closeAfterWrite.formatsPublished));
    static_cast<void>(
        context.expectEq("published side effect survives close failure", std::string{"committed despite close failure"}, Clipboard::readText().text));

    Desktop::TestHooks::failNext(Desktop::TestHooks::FailurePoint::ClipboardClose);
    const auto closeAfterClear = Clipboard::clear();
    static_cast<void>(context.expectEq("close failure after clear is reported", ErrorCode::CloseFailed, closeAfterClear.status.code));
    static_cast<void>(context.expectTrue("close failure preserves cleared side effect", closeAfterClear.cleared));
    Desktop::TestHooks::resetFailures();
#endif

    std::atomic_bool holderReady = false;
    std::atomic_bool releaseHolder = false;
    std::atomic_bool holderOpened = false;
    std::thread holder(
        [&]()
        {
            HWND owner = CreateWindowExW(
                0,
                L"STATIC",
                L"GameWIP Clipboard Contention",
                WS_POPUP,
                0,
                0,
                0,
                0,
                nullptr,
                nullptr,
                GetModuleHandleW(nullptr),
                nullptr);
            holderOpened.store(owner != nullptr && OpenClipboard(owner) != FALSE);
            holderReady.store(true);
            while (!releaseHolder.load())
                std::this_thread::yield();
            if (holderOpened.load())
                static_cast<void>(CloseClipboard());
            if (owner != nullptr)
                static_cast<void>(DestroyWindow(owner));
        });
    while (!holderReady.load())
        std::this_thread::yield();
    if (holderOpened.load())
    {
        const auto before = std::chrono::steady_clock::now();
        const auto boundedBusy = Clipboard::getFormats(std::chrono::milliseconds{20});
        const auto elapsed = std::chrono::steady_clock::now() - before;
        static_cast<void>(context.expectEq("finite timeout reports contention", ErrorCode::ResourceBusy, boundedBusy.status.code));
        static_cast<void>(context.expectTrue("finite timeout is bounded without busy spin", elapsed < std::chrono::seconds{1}));
    }
    else
        context.skip("finite Clipboard contention timeout", "native holder could not acquire Clipboard");
    releaseHolder.store(true);
    holder.join();

    std::atomic<ErrorCode> threadResult{ErrorCode::Unknown};
    std::thread ordinaryThread(
        [&]()
        {
            threadResult.store(Clipboard::writeText("ordinary thread").status.code);
        });
    ordinaryThread.join();
    static_cast<void>(context.expectEq("ordinary non-Window thread may use Clipboard", ErrorCode::Success, threadResult.load()));
    static_cast<void>(Clipboard::clear());
}
