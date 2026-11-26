/// @file metadata_test.inl
/// @brief Focused FileSystem metadata tests.

/// @brief Verifies existence, kind, missing-entry, and invalid-path query behavior.
void testBasicEntryQueries(TestSupport::Context &context, const std::filesystem::path &root)
{
    const std::filesystem::path directory = root / "basic" / "directory";
    const std::filesystem::path file = root / "basic" / "file.txt";
    const std::filesystem::path missing = root / "basic" / "missing.txt";

    static_cast<void>(context.expectTrue("basic fixture directory creation succeeds", TestSupport::createDirectories(directory).ok()));
    static_cast<void>(context.expectTrue("basic fixture file write succeeds", TestSupport::writeTextFile(file, "payload").ok()));

    const auto fileExists = FileSystem::exists(file, queryOptions(FileSystem::Types::SymlinkPolicy::FollowAll));
    static_cast<void>(context.expectTrue("exists reports regular file success", fileExists.ok()));
    static_cast<void>(context.expectTrue("exists reports regular file true", fileExists.ok() && fileExists.value()));

    const auto missingExists = FileSystem::exists(missing, queryOptions(FileSystem::Types::SymlinkPolicy::FollowAll));
    static_cast<void>(context.expectTrue("exists reports missing path success", missingExists.ok()));
    static_cast<void>(context.expectFalse("exists reports missing path false", missingExists.ok() && missingExists.value()));

    const auto regularFile = FileSystem::isRegularFile(file);
    static_cast<void>(context.expectTrue("default strict regular-file query succeeds", regularFile.ok()));
    static_cast<void>(context.expectTrue("default strict regular-file query returns true", regularFile.ok() && regularFile.value()));

    const auto directoryResult = FileSystem::isDirectory(directory);
    static_cast<void>(context.expectTrue("default strict directory query succeeds", directoryResult.ok()));
    static_cast<void>(context.expectTrue("default strict directory query returns true", directoryResult.ok() && directoryResult.value()));

    const auto symlinkResult = FileSystem::isSymlink(file);
    static_cast<void>(context.expectTrue("default strict symlink query succeeds for normal file", symlinkResult.ok()));
    static_cast<void>(context.expectFalse("default strict symlink query returns false for normal file", symlinkResult.ok() && symlinkResult.value()));

    const auto emptyPath = FileSystem::exists({});
    static_cast<void>(context.expectEq("empty path is invalid", ErrorCode::InvalidArgument, emptyPath.status().code()));
}

/// @brief Verifies metadata queries, directory creation, listing filters, and path helpers.
void testMetadataDirectoriesAndListing(TestSupport::Context &context, const std::filesystem::path &root)
{
    const std::filesystem::path directory = root / "directories" / "nested";
    const std::filesystem::path file = directory / "file.txt";

    const auto createStatus = FileSystem::createDirectories(directory);
    static_cast<void>(context.expectTrue("createDirectories succeeds", createStatus.ok()));

    const auto writeStatus = FileSystem::writeAllText(file, "payload");
    static_cast<void>(context.expectTrue("writeAllText creates file", writeStatus.ok()));
    if(writeStatus.hasValue())
    {
        static_cast<void>(context.expectEq("writeAllText reports payload size", std::size_t{7}, writeStatus.value().bytesWritten));
    }

    // Query size, timestamps, and read-only metadata.
    const auto size = FileSystem::getFileSize(file);
    static_cast<void>(context.expectTrue("getFileSize succeeds", size.ok()));
    if(size.ok())
    {
        static_cast<void>(context.expectEq("getFileSize reports bytes", std::uint64_t{7}, size.value()));
    }

    const auto lastWrite = FileSystem::getLastWriteTime(file);
    static_cast<void>(context.expectTrue("getLastWriteTime succeeds", lastWrite.ok()));

    const auto readOnly = FileSystem::isReadOnly(file);
    static_cast<void>(context.expectTrue("isReadOnly succeeds", readOnly.ok()));
    static_cast<void>(context.expectFalse("new file is writable", readOnly.ok() && readOnly.value()));

    static_cast<void>(context.expectTrue("setReadOnly true succeeds", FileSystem::setReadOnly(file, true).ok()));
    const auto readOnlyAfterSet = FileSystem::isReadOnly(file);
    static_cast<void>(context.expectTrue("isReadOnly after set succeeds", readOnlyAfterSet.ok()));
    static_cast<void>(context.expectTrue("setReadOnly true updates metadata", readOnlyAfterSet.ok() && readOnlyAfterSet.value()));
    static_cast<void>(context.expectTrue("setReadOnly false succeeds", FileSystem::setReadOnly(file, false).ok()));
    const auto writableAfterSet = FileSystem::isReadOnly(file);
    static_cast<void>(context.expectTrue("isReadOnly after clear succeeds", writableAfterSet.ok()));
    static_cast<void>(context.expectFalse("setReadOnly false updates metadata", writableAfterSet.ok() && writableAfterSet.value()));

    // Check listing contents and entry limits.
    const auto listing = FileSystem::listDirectory(directory);
    static_cast<void>(context.expectTrue("listDirectory succeeds", listing.ok()));
    if(listing.hasValue())
    {
        static_cast<void>(context.expectEq("listDirectory returns one entry", std::size_t{1}, listing.value().size()));
        if(!listing.value().empty())
        {
            static_cast<void>(context.expectEq("listDirectory entry is file", FileSystem::Types::EntryKind::RegularFile, listing.value()[0].info.kind));
        }
    }

    const auto limitedListing = FileSystem::listDirectory(directory, FileSystem::Types::Directory::ListOptions{.maxEntries = 0});
    static_cast<void>(
        context.expectEq("listDirectory maxEntries returns SizeLimitExceeded", ErrorCode::SizeLimitExceeded, limitedListing.status().code()));
    static_cast<void>(context.expectTrue("listDirectory zero limit retains no entries", limitedListing.hasValue() && limitedListing.value().empty()));

    const std::filesystem::path listingLimitDirectory = root / "directories" / "listing-limit";
    static_cast<void>(context.expectTrue("listing-limit directory creation succeeds", FileSystem::createDirectories(listingLimitDirectory).ok()));
    static_cast<void>(context.expectTrue(
        "listing-limit first file write succeeds",
        FileSystem::writeAllText(listingLimitDirectory / "first.txt", "first").ok()));
    const auto exactListing = FileSystem::listDirectory(listingLimitDirectory, FileSystem::Types::Directory::ListOptions{.maxEntries = 1});
    static_cast<void>(context.expectTrue("exact listing limit succeeds", exactListing.ok()));
    if(exactListing.hasValue())
    {
        static_cast<void>(context.expectEq("exact listing limit returns N entries", std::size_t{1}, exactListing.value().size()));
    }
    static_cast<void>(context.expectTrue(
        "listing-limit second file write succeeds",
        FileSystem::writeAllText(listingLimitDirectory / "second.txt", "second").ok()));
    const auto partialListing = FileSystem::listDirectory(listingLimitDirectory, FileSystem::Types::Directory::ListOptions{.maxEntries = 1});
    static_cast<void>(context.expectEq(
        "listing with an additional matching entry reports SizeLimitExceeded",
        ErrorCode::SizeLimitExceeded,
        partialListing.status().code()));
    static_cast<void>(context.expectTrue("partial listing preserves N entries", partialListing.hasValue() && partialListing.value().size() == 1));

    // Check cursor ownership, exhaustion, and entry limits.
    FileSystem::DirectoryCursor cursor;
    static_cast<void>(context.expectEq("closed directory cursor reports NotOpen", ErrorCode::NotOpen, cursor.next().status().code()));

    if(!expectStatus(context, "directory cursor opens", cursor.open(directory)))
    {
        return;
    }

    static_cast<void>(context.expectEq("open directory cursor rejects AlreadyOpen", ErrorCode::AlreadyOpen, cursor.open(directory).status().code()));
    FileSystem::DirectoryCursor movedCursor = std::move(cursor);
    // The public move contract explicitly guarantees that the source cursor is closed.
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    static_cast<void>(context.expectFalse("moved-from directory cursor is closed", cursor.isOpen()));
    const auto cursorEntry = movedCursor.next();
    static_cast<void>(context.expectTrue("directory cursor returns an entry", cursorEntry.ok() && cursorEntry.value().has_value()));

    if(cursorEntry.ok() && cursorEntry.value().has_value())
    {
        static_cast<void>(context.expectEq("moved directory cursor preserves entry path", file, cursorEntry.value()->path));
    }
    const auto cursorEnd = movedCursor.next();
    static_cast<void>(context.expectTrue("directory cursor reports successful exhaustion", cursorEnd.ok() && !cursorEnd.value().has_value()));
    const auto repeatedEnd = movedCursor.next();
    static_cast<void>(context.expectTrue("directory cursor exhaustion remains successful", repeatedEnd.ok() && !repeatedEnd.value().has_value()));
    static_cast<void>(context.expectTrue("directory cursor closes", movedCursor.close().ok()));
    static_cast<void>(context.expectFalse("closed directory cursor is not open", movedCursor.isOpen()));

    FileSystem::DirectoryCursor limitedCursor;
    static_cast<void>(context.expectTrue(
        "limited directory cursor opens",
        limitedCursor.open(directory, FileSystem::Types::Directory::ListOptions{.maxEntries = 0}).ok()));
    static_cast<void>(context.expectEq("directory cursor enforces maxEntries", ErrorCode::SizeLimitExceeded, limitedCursor.next().status().code()));
    const auto zeroLimitNext = limitedCursor.next();
    static_cast<void>(context.expectFalse("zero-limit cursor returns no entry", zeroLimitNext.hasValue() && zeroLimitNext.value().has_value()));
    static_cast<void>(
        context.expectEq("directory cursor keeps the terminal limit status", ErrorCode::SizeLimitExceeded, limitedCursor.next().status().code()));

    FileSystem::DirectoryCursor exactCursor;
    static_cast<void>(context.expectTrue(
        "exact-limit cursor opens",
        exactCursor.open(listingLimitDirectory, FileSystem::Types::Directory::ListOptions{.maxEntries = 1}).ok()));
    const auto exactCursorEntry = exactCursor.next();
    static_cast<void>(context.expectTrue("exact-limit cursor returns Nth entry", exactCursorEntry.ok() && exactCursorEntry.value().has_value()));
    const auto limitedCursorEnd = exactCursor.next();
    static_cast<void>(context.expectEq(
        "cursor with an additional matching entry reports SizeLimitExceeded",
        ErrorCode::SizeLimitExceeded,
        limitedCursorEnd.status().code()));
    static_cast<void>(context.expectFalse("cursor limit result contains no extra entry", limitedCursorEnd.hasValue() && limitedCursorEnd.value().has_value()));
    static_cast<void>(context.expectEq("cursor limit status remains terminal", ErrorCode::SizeLimitExceeded, exactCursor.next().status().code()));
}
