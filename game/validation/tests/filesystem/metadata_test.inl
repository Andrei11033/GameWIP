/// @file metadata_test.inl
/// @brief Focused filesystem metadata correctness suites.

/// @brief Verifies existence, kind, missing-entry, and invalid-path query behavior.
void testBasicEntryQueries(TestSupport::Context &context, const std::filesystem::path &root)
{
    const std::filesystem::path directory = root / "basic" / "directory";
    const std::filesystem::path file = root / "basic" / "file.txt";
    const std::filesystem::path missing = root / "basic" / "missing.txt";

    static_cast<void>(context.expectTrue("basic fixture directory creation succeeds", TestSupport::createDirectories(directory).ok()));
    static_cast<void>(context.expectTrue("basic fixture file write succeeds", TestSupport::writeTextFile(file, "payload").ok()));

    const auto fileExists = FileSystem::exists(file, queryOptions(FileSystem::Types::SymlinkPolicy::FollowAll));
    static_cast<void>(context.expectTrue("exists reports regular file success", fileExists.status.ok()));
    static_cast<void>(context.expectTrue("exists reports regular file true", fileExists.value));

    const auto missingExists = FileSystem::exists(missing, queryOptions(FileSystem::Types::SymlinkPolicy::FollowAll));
    static_cast<void>(context.expectTrue("exists reports missing path success", missingExists.status.ok()));
    static_cast<void>(context.expectFalse("exists reports missing path false", missingExists.value));

    const auto regularFile = FileSystem::isRegularFile(file);
    static_cast<void>(context.expectTrue("default strict regular-file query succeeds", regularFile.status.ok()));
    static_cast<void>(context.expectTrue("default strict regular-file query returns true", regularFile.value));

    const auto directoryResult = FileSystem::isDirectory(directory);
    static_cast<void>(context.expectTrue("default strict directory query succeeds", directoryResult.status.ok()));
    static_cast<void>(context.expectTrue("default strict directory query returns true", directoryResult.value));

    const auto symlinkResult = FileSystem::isSymlink(file);
    static_cast<void>(context.expectTrue("default strict symlink query succeeds for normal file", symlinkResult.status.ok()));
    static_cast<void>(context.expectFalse("default strict symlink query returns false for normal file", symlinkResult.value));

    const auto emptyPath = FileSystem::exists({});
    static_cast<void>(context.expectEq("empty path is invalid", ErrorCode::InvalidArgument, emptyPath.status.code));
}

/// @brief Verifies metadata queries, directory creation, listing filters, and path helpers.
void testMetadataDirectoriesAndListing(TestSupport::Context &context, const std::filesystem::path &root)
{
    const std::filesystem::path directory = root / "directories" / "nested";
    const std::filesystem::path file = directory / "file.txt";

    const IO::Types::Status createStatus = FileSystem::createDirectories(directory);
    static_cast<void>(context.expectTrue("createDirectories succeeds", createStatus.ok()));

    const IO::Types::WriteResult writeStatus = FileSystem::writeAllText(file, "payload");
    static_cast<void>(context.expectTrue("writeAllText creates file", writeStatus.status.ok()));
    static_cast<void>(context.expectEq("writeAllText reports payload size", std::size_t{7}, writeStatus.bytesWritten));

    const IO::Types::SizeResult size = FileSystem::getFileSize(file);
    static_cast<void>(context.expectTrue("getFileSize succeeds", size.status.ok()));
    static_cast<void>(context.expectEq("getFileSize reports bytes", std::uint64_t{7}, size.sizeBytes));

    const auto lastWrite = FileSystem::getLastWriteTime(file);
    static_cast<void>(context.expectTrue("getLastWriteTime succeeds", lastWrite.status.ok()));

    const auto readOnly = FileSystem::isReadOnly(file);
    static_cast<void>(context.expectTrue("isReadOnly succeeds", readOnly.status.ok()));
    static_cast<void>(context.expectFalse("new file is writable", readOnly.value));

    static_cast<void>(context.expectTrue("setReadOnly true succeeds", FileSystem::setReadOnly(file, true).ok()));
    const auto readOnlyAfterSet = FileSystem::isReadOnly(file);
    static_cast<void>(context.expectTrue("isReadOnly after set succeeds", readOnlyAfterSet.status.ok()));
    static_cast<void>(context.expectTrue("setReadOnly true updates metadata", readOnlyAfterSet.value));
    static_cast<void>(context.expectTrue("setReadOnly false succeeds", FileSystem::setReadOnly(file, false).ok()));
    const auto writableAfterSet = FileSystem::isReadOnly(file);
    static_cast<void>(context.expectTrue("isReadOnly after clear succeeds", writableAfterSet.status.ok()));
    static_cast<void>(context.expectFalse("setReadOnly false updates metadata", writableAfterSet.value));

    const auto listing = FileSystem::listDirectory(directory);
    static_cast<void>(context.expectTrue("listDirectory succeeds", listing.status.ok()));
    static_cast<void>(context.expectEq("listDirectory returns one entry", std::size_t{1}, listing.entries.size()));
    if (!listing.entries.empty())
    {
        static_cast<void>(context.expectEq("listDirectory entry is file", FileSystem::Types::EntryKind::RegularFile, listing.entries[0].info.kind));
    }

    const auto limitedListing = FileSystem::listDirectory(directory, FileSystem::Types::ListDirectoryOptions{.maxEntries = 0});
    static_cast<void>(
        context.expectEq("listDirectory maxEntries returns SizeLimitExceeded", ErrorCode::SizeLimitExceeded, limitedListing.status.code));

    FileSystem::DirectoryCursor cursor;
    static_cast<void>(context.expectEq("closed directory cursor reports NotOpen", ErrorCode::NotOpen, cursor.next().status.code));
    static_cast<void>(context.expectTrue("directory cursor opens", cursor.open(directory).ok()));
    static_cast<void>(context.expectEq("open directory cursor rejects AlreadyOpen", ErrorCode::AlreadyOpen, cursor.open(directory).code));
    FileSystem::DirectoryCursor movedCursor = std::move(cursor);
    const auto cursorEntry = movedCursor.next();
    static_cast<void>(context.expectTrue("directory cursor returns an entry", cursorEntry.status.ok() && cursorEntry.hasEntry));
    const auto cursorEnd = movedCursor.next();
    static_cast<void>(context.expectTrue("directory cursor reports successful exhaustion", cursorEnd.status.ok() && !cursorEnd.hasEntry));
    static_cast<void>(context.expectTrue("directory cursor closes", movedCursor.close().ok()));
    static_cast<void>(context.expectFalse("closed directory cursor is not open", movedCursor.isOpen()));

    FileSystem::DirectoryCursor limitedCursor;
    static_cast<void>(context.expectTrue(
        "limited directory cursor opens",
        limitedCursor.open(directory, FileSystem::Types::ListDirectoryOptions{.maxEntries = 0}).ok()));
    static_cast<void>(context.expectEq("directory cursor enforces maxEntries", ErrorCode::SizeLimitExceeded, limitedCursor.next().status.code));
    static_cast<void>(
        context.expectEq("directory cursor keeps the terminal limit status", ErrorCode::SizeLimitExceeded, limitedCursor.next().status.code));
}
