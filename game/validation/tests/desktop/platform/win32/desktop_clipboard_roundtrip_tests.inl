/// @file desktop_clipboard_roundtrip_tests.inl
/// @brief Clipboard native round-trip tests.

/// ------------------------------------------------------------
/// Clipboard round trips
/// ------------------------------------------------------------

void testClipboardRoundTrips(TestSupport::Context &context)
{
    // Round-trip text, including non-BMP characters and empty content.
    const auto initialClear = Clipboard::clear();
    static_cast<void>(context.expectTrue("Clipboard clears without a GameWIP Window", initialClear.ok() && initialClear.value().cleared));
    const auto absentText = Clipboard::hasFormat(Transfer::FormatView::text());
    static_cast<void>(context.expectTrue("absent predicate is successful false", absentText.ok() && !absentText.value()));

    constexpr std::string_view text = "Clipboard UTF-8: caf\xC3\xA9 \xF0\x9F\x98\x80";
    const auto textWrite = Clipboard::writeText(text);
    if(context.expectTrue("UTF-8 text publishes", textWrite.ok()))
    {
        static_cast<void>(context.expectEq("single text write publishes all", ClipboardTypes::CommitState::Published, textWrite.value().commitState));
        static_cast<void>(context.expectEq("single text count is one", std::size_t{1}, textWrite.value().formatsPublished));
    }
    const auto textRead = Clipboard::readText();
    if(context.expectTrue("UTF-8 text reads", textRead.ok()))
    {
        static_cast<void>(context.expectEq("UTF-8 and non-BMP text round trip", std::string{text}, textRead.value()));
    }
    const auto textAvailable = Clipboard::hasFormat(Transfer::FormatView::text());
    static_cast<void>(context.expectTrue("text predicate is available", textAvailable.ok() && textAvailable.value()));

    const auto emptyTextWrite = Clipboard::writeText("");
    static_cast<void>(context.expectTrue("empty text publishes", emptyTextWrite.ok()));
    const auto emptyTextRead = Clipboard::readText();
    if(context.expectTrue("empty text reads", emptyTextRead.ok()))
    {
        static_cast<void>(context.expectTrue("empty text remains empty", emptyTextRead.value().empty()));
    }

    // Round-trip absolute file paths without opening the files.
    const std::array paths{
        GameWIP::FileSystem::Types::Path{L"C:\\GameWIP Clipboard Nonexistent.txt"},
        GameWIP::FileSystem::Types::Path{L"C:\\Unicode-\u03A9-\u4E2D.dat"}};
    const auto filesWrite = Clipboard::writeFiles(paths);
    static_cast<void>(context.expectTrue("absolute nonexistent Unicode paths publish without IO", filesWrite.ok()));
    const auto filesRead = Clipboard::readFiles();
    if(context.expectTrue("file list reads", filesRead.ok()))
    {
        static_cast<void>(context.expectEq("file list preserves order and spelling", std::vector(paths.begin(), paths.end()), filesRead.value()));
    }

    // Round-trip padded RGBA pixels and inspect the native DIB metadata.
    const std::array<std::byte, 24> paddedPixels{
        std::byte{0x10}, std::byte{0x20}, std::byte{0x30}, std::byte{0x40}, std::byte{0x50}, std::byte{0x60}, std::byte{0x70}, std::byte{0x80},
        std::byte{0xCC}, std::byte{0xCC}, std::byte{0xCC}, std::byte{0xCC}, std::byte{0x90}, std::byte{0xA0}, std::byte{0xB0}, std::byte{0xC0},
        std::byte{0xD0}, std::byte{0xE0}, std::byte{0xF0}, std::byte{0xFF}, std::byte{0xDD}, std::byte{0xDD}, std::byte{0xDD}, std::byte{0xDD}};
    const auto imageWrite = Clipboard::writeImage({{2, 2}, 12, paddedPixels});
    static_cast<void>(context.expectTrue("padded RGBA8 image publishes", imageWrite.ok()));
    BITMAPV5HEADER publishedHeader{};

    if(context.expectTrue("published DIBV5 header is readable", readNativeDibV5Header(context, publishedHeader)))
    {
        static_cast<void>(context.expectEq("published DIBV5 uses image rendering intent", DWORD{LCS_GM_IMAGES}, publishedHeader.bV5Intent));
    }
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
    if(context.expectTrue("DIBV5 image reads", imageRead.ok()))
    {
        static_cast<void>(context.expectEq("image dimensions round trip", Desktop::Types::PixelSize{2, 2}, imageRead.value().size));
        static_cast<void>(context.expectEq("image read is tightly packed with alpha", expectedPixels, imageRead.value().rgba8));
    }

    // Round-trip opaque custom data and verify format enumeration.
    const std::array customBytes{std::byte{0x00}, std::byte{0x11}, std::byte{0xFE}, std::byte{0x7F}};
    const auto customWrite = Clipboard::writeCustomData({"MyCompany.Material", customBytes});
    static_cast<void>(context.expectTrue("arbitrary named binary format publishes", customWrite.ok()));
    const auto customRead = Clipboard::readCustomData("mycompany.material");
    if(context.expectTrue("custom names use case-insensitive Win32 identity", customRead.ok()))
    {
        static_cast<void>(context.expectEq(
            "opaque bytes including null round trip exactly",
            std::vector(customBytes.begin(), customBytes.end()),
            customRead.value()));
    }
    const auto formats = Clipboard::getFormats();
    static_cast<void>(context.expectTrue("custom format enumeration succeeds", formats.ok()));
    static_cast<void>(context.expectTrue(
        "enumeration owns the native custom name",
        formats.ok() &&
            std::ranges::find(formats.value(), Transfer::Format{Transfer::FormatKind::Custom, "MyCompany.Material"}) != formats.value().end()));
    static_cast<void>(context.expectEq(
        "missing custom read is NotFound",
        StatusCode::NotFound,
        Clipboard::readCustomData("GameWIP.Validation.DefinitelyMissing").status().code()));

    const auto emptyCustomWrite = Clipboard::writeCustomData({"GameWIP.Validation.Empty", {}});
    static_cast<void>(
        context.expectEq("zero-byte custom publication reports Win32 limitation", StatusCode::Unsupported, emptyCustomWrite.status().code()));
    if(context.expectTrue("zero-byte custom failure retains progress", emptyCustomWrite.hasValue()))
    {
        static_cast<void>(
            context.expectEq("zero-byte custom failure does not mutate", ClipboardTypes::CommitState::NotStarted, emptyCustomWrite.value().commitState));
    }
    const std::array<Transfer::ItemView, 1> emptyCustomItems{{Transfer::CustomView{"GameWIP.Validation.Empty.Generic", {}}}};
    const auto emptyGenericWrite = Clipboard::write(emptyCustomItems);
    static_cast<void>(
        context.expectEq("zero-byte generic custom publication is unsupported", StatusCode::Unsupported, emptyGenericWrite.status().code()));
    if(context.expectTrue("zero-byte generic custom failure retains progress", emptyGenericWrite.hasValue()))
    {
        static_cast<void>(context.expectEq(
            "zero-byte generic custom failure does not mutate",
            ClipboardTypes::CommitState::NotStarted,
            emptyGenericWrite.value().commitState));
    }

    const std::array<wchar_t, 2> malformedWide{static_cast<wchar_t>(0xD800), L'\0'};
    const auto malformedWideBytes = std::as_bytes(std::span{malformedWide});

    if(context.expectTrue("native malformed Unicode fixture publishes", publishNativeClipboardData(context, CF_UNICODETEXT, malformedWideBytes)))
    {
        static_cast<void>(
            context.expectEq("malformed native Unicode is EncodingFailed", StatusCode::EncodingFailed, Clipboard::readText().status().code()));
    }

    // Reject malformed external text and convert supported foreign image data.
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

    if(context.expectTrue("foreign BI_RGB fixture publishes", publishNativeClipboardData(context, CF_DIB, rgbDib)))
    {
        const auto rgbRead = Clipboard::readImage();
        if(context.expectTrue("foreign BI_RGB image reads", rgbRead.ok()))
        {
            if(context.expectEq("foreign BI_RGB image has four bytes", std::size_t{4}, rgbRead.value().rgba8.size()))
            {
                static_cast<void>(context.expectEq("foreign RGB red converts", std::byte{0x11}, rgbRead.value().rgba8[0]));
                static_cast<void>(context.expectEq("foreign RGB alpha becomes opaque", std::byte{0xFF}, rgbRead.value().rgba8[3]));
            }
        }
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

    if(context.expectTrue("valid nonstandard-mask DIB fixture publishes", publishNativeClipboardData(context, CF_DIBV5, unsupportedDib)))
    {
        static_cast<void>(
            context.expectEq("valid nonstandard-mask image is Unsupported", StatusCode::Unsupported, Clipboard::readImage().status().code()));
    }
}

/// ------------------------------------------------------------
/// Clipboard contention and failures
/// ------------------------------------------------------------
