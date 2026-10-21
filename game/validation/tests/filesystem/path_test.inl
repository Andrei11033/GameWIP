/// @file path_test.inl
/// @brief Focused filesystem path correctness suites.

/// @brief Verifies explicit UTF-8 path conversion with non-ASCII fixture names.
void testUtf8PathConversion(TestSupport::Context &context, const std::filesystem::path &root)
{
    const std::string utf8Name = std::string{"unicode_"} + "\xD1\x84\xD0\xB0\xD0\xB9\xD0\xBB.txt";
    const auto namePath = FileSystem::pathFromUtf8(utf8Name);

    if (!expectStatus(context, "pathFromUtf8 succeeds for non-ASCII filename", namePath.status))
    {
        return;
    }

    const std::filesystem::path path = root / "unicode" / namePath.path;
    static_cast<void>(context.expectTrue("write non-ASCII path succeeds", FileSystem::writeAllText(path, "text").status.ok()));
    const auto roundTrip = FileSystem::pathToUtf8(path.filename());
    static_cast<void>(context.expectTrue("pathToUtf8 succeeds for non-ASCII filename", roundTrip.status.ok()));
    static_cast<void>(context.expectEq("UTF-8 path round-trips", utf8Name, roundTrip.utf8));

    const std::u8string utf8Name8 = u8"unicode_файл8.txt";
    const auto namePath8 = FileSystem::pathFromUtf8(std::u8string_view(utf8Name8));
    static_cast<void>(context.expectTrue("pathFromUtf8 accepts explicit char8_t text", namePath8.status.ok()));

    if (namePath8.status.ok())
    {
        const auto roundTrip8 = FileSystem::pathToUtf8(namePath8.path);
        static_cast<void>(context.expectTrue("char8_t path conversion round-trips", roundTrip8.status.ok()));
        static_cast<void>(context.expectEq("char8_t path preserves UTF-8", std::string(utf8Name8.begin(), utf8Name8.end()), roundTrip8.utf8));
    }

    std::string malformedPath{"bad_"};
    malformedPath.push_back(static_cast<char>(0xC0));
    malformedPath.push_back(static_cast<char>(0xAF));
    const auto malformedResult = FileSystem::pathFromUtf8(malformedPath);
    static_cast<void>(context.expectEq("pathFromUtf8 rejects malformed UTF-8", ErrorCode::EncodingFailed, malformedResult.status.code));

#if !defined(_WIN32)
    const std::filesystem::path invalidNativePath{std::string(1, static_cast<char>(0xFF))};
    const auto invalidNativeResult = FileSystem::pathToUtf8(invalidNativePath);
    static_cast<void>(
        context.expectEq("pathToUtf8 rejects invalid native byte spelling", ErrorCode::EncodingFailed, invalidNativeResult.status.code));
    static_cast<void>(context.expectTrue("failed pathToUtf8 returns empty text", invalidNativeResult.utf8.empty()));
#endif
}
