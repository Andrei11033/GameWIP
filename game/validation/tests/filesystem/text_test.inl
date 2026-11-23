/// @file text_test.inl
/// @brief FileSystem UTF-8 text test.

/// @brief Verifies strict UTF-8 content helpers and pre-side-effect text validation.
void testTextContracts(TestSupport::Context &context, const std::filesystem::path &root)
{
    const std::filesystem::path directory = root / "text-contracts";
    const std::filesystem::path file = directory / "content.txt";
    static_cast<void>(context.expectTrue("text contract fixture write succeeds", FileSystem::writeAllText(file, "preserved").status.ok()));

    // Read malformed bytes and retain the valid prefix.
    std::string malformed{"prefix "};
    malformed.push_back(static_cast<char>(0xE2));
    malformed.push_back(static_cast<char>(0x28));
    malformed.push_back(static_cast<char>(0xA1));

    const std::filesystem::path malformedFile = directory / "malformed.bin";
    const auto rawWrite = FileSystem::writeAllBytes(malformedFile, bytesOf(malformed));

    if(!expectStatus(context, "byte helper writes malformed fixture bytes", rawWrite.status))
    {
        return;
    }

    const auto malformedRead = FileSystem::readAllText(malformedFile);
    static_cast<void>(context.expectEq("readAllText rejects malformed UTF-8", ErrorCode::EncodingFailed, malformedRead.status.code));
    static_cast<void>(context.expectEq("readAllText preserves valid prefix", std::string{"prefix "}, malformedRead.text));

    // Preserve embedded NULs and valid multibyte text exactly.
    std::string validText{"A\0", 2};
    validText.append("\xF0\x9F\x98\x80", 4);
    const auto validWrite = FileSystem::writeAllText(file, validText);
    static_cast<void>(context.expectTrue("writeAllText accepts embedded NUL and multibyte UTF-8", validWrite.status.ok()));
    const auto validRead = FileSystem::readAllText(file);
    static_cast<void>(context.expectTrue("readAllText returns valid multibyte UTF-8", validRead.status.ok()));
    static_cast<void>(context.expectEq("valid text round-trips exactly", validText, validRead.text));

    // Reject malformed writes before changing files or creating directories.
    if(!expectStatus(context, "restore preservation fixture", FileSystem::writeAllText(file, "preserved").status))
    {
        return;
    }

    const auto invalidWrite = FileSystem::writeAllText(file, malformed);
    static_cast<void>(context.expectEq("writeAllText rejects malformed UTF-8", ErrorCode::EncodingFailed, invalidWrite.status.code));
    static_cast<void>(context.expectEq("invalid write reports zero progress", std::size_t{0}, invalidWrite.bytesWritten));
    expectFileText(context, "invalid write preserves existing content", file, "preserved");

    const std::filesystem::path missingWrite = root / "invalid-write-parent" / "file.txt";
    const auto missingInvalidWrite = FileSystem::writeAllText(missingWrite, malformed);
    static_cast<void>(
        context.expectEq("invalid write to missing path is EncodingFailed", ErrorCode::EncodingFailed, missingInvalidWrite.status.code));
    expectPredicate(context, "invalid write creates no parent", FileSystem::exists(missingWrite.parent_path()), false);

    const auto invalidAppend = FileSystem::appendText(file, malformed);
    static_cast<void>(context.expectEq("appendText rejects malformed UTF-8", ErrorCode::EncodingFailed, invalidAppend.status.code));
    static_cast<void>(context.expectEq("invalid append reports zero progress", std::size_t{0}, invalidAppend.bytesWritten));
    expectFileText(context, "invalid append preserves content", file, "preserved");

    const std::filesystem::path missingAppend = root / "invalid-append-parent" / "file.txt";
    const auto missingInvalidAppend = FileSystem::appendText(missingAppend, malformed);
    static_cast<void>(
        context.expectEq("invalid append to missing path is EncodingFailed", ErrorCode::EncodingFailed, missingInvalidAppend.status.code));
    expectPredicate(context, "invalid append creates no parent", FileSystem::exists(missingAppend.parent_path()), false);

    const auto invalidAtomic = FileSystem::writeAllTextAtomic(file, malformed);
    static_cast<void>(context.expectEq("writeAllTextAtomic rejects malformed UTF-8", ErrorCode::EncodingFailed, invalidAtomic.code));
    expectFileText(context, "invalid atomic write preserves destination", file, "preserved");

    const std::filesystem::path missingAtomic = root / "invalid-atomic-parent" / "file.txt";
    const auto missingInvalidAtomic = FileSystem::writeAllTextAtomic(missingAtomic, malformed);
    static_cast<void>(
        context.expectEq("invalid atomic write to missing path is EncodingFailed", ErrorCode::EncodingFailed, missingInvalidAtomic.code));
    expectPredicate(context, "invalid atomic write creates no parent", FileSystem::exists(missingAtomic.parent_path()), false);

    // Validate UTF-8 temporary-name prefixes before replacement.
    FileSystem::Types::File::AtomicWriteOptions unicodePrefixOptions;
    unicodePrefixOptions.temporaryNamePrefix = "tmp_\xD1\x84_";
    const auto unicodePrefixWrite = FileSystem::writeAllTextAtomic(file, "unicode-prefix", unicodePrefixOptions);
    static_cast<void>(context.expectTrue("atomic write accepts UTF-8 temporary prefix", unicodePrefixWrite.ok()));

    FileSystem::Types::File::AtomicWriteOptions malformedPrefixOptions;
    malformedPrefixOptions.temporaryNamePrefix.assign(1, static_cast<char>(0xFF));
    const auto malformedPrefixWrite = FileSystem::writeAllTextAtomic(file, "must-not-write", malformedPrefixOptions);
    static_cast<void>(
        context.expectEq("atomic write rejects malformed UTF-8 temporary prefix", ErrorCode::EncodingFailed, malformedPrefixWrite.code));
    expectFileText(context, "invalid temporary prefix preserves destination", file, "unicode-prefix");
}
