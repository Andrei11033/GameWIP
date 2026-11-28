/// @file mutation_test.inl
/// @brief FileSystem mutation test.

/// @brief Verifies metadata mutation, copy, move, resize, truncate, and removal operations.
void testMutationCopyMoveAndRemoval(TestSupport::Context &context, const std::filesystem::path &root)
{
    const std::filesystem::path directory = root / "mutations";
    const std::filesystem::path source = directory / "source.txt";
    const std::filesystem::path copy = directory / "copy.txt";
    const std::filesystem::path metadataCopy = directory / "metadata-copy.txt";
    const std::filesystem::path moved = directory / "moved.txt";
    const std::filesystem::path emptyDirectory = directory / "empty";
    const std::filesystem::path tree = directory / "tree";

    static_cast<void>(context.expectTrue("create mutation directory", FileSystem::createDirectories(directory).ok()));

    // Resize and copy the source file.
    if(!expectStatus(context, "write mutation source", FileSystem::writeAllText(source, "abcdef")))
    {
        return;
    }

    static_cast<void>(context.expectTrue("resizeFile succeeds", FileSystem::resizeFile(source, 3).ok()));
    const auto resized = FileSystem::readAllText(source);
    static_cast<void>(context.expectTrue("read resized file succeeds", resized.ok()));
    if(resized.ok())
    {
        static_cast<void>(context.expectEq("resizeFile truncates content", std::string{"abc"}, resized.value()));
    }

    static_cast<void>(context.expectTrue("copyFile succeeds", FileSystem::copyFile(source, copy).ok()));
    const auto copied = FileSystem::readAllText(copy);
    static_cast<void>(context.expectTrue("read copied file succeeds", copied.ok()));
    if(copied.ok())
    {
        static_cast<void>(context.expectEq("copyFile preserves content", std::string{"abc"}, copied.value()));
    }

    static_cast<void>(context.expectTrue("source read-only setup succeeds", FileSystem::setReadOnly(source, true).ok()));
    FileSystem::Types::File::CopyOptions copyOptions;
    copyOptions.metadataMode = FileSystem::Types::File::CopyMetadataMode::Basic;
    static_cast<void>(context.expectTrue("copyFile basic metadata succeeds", FileSystem::copyFile(source, metadataCopy, copyOptions).ok()));
    const auto metadataCopied = FileSystem::readAllText(metadataCopy);
    static_cast<void>(context.expectTrue("read metadata copy succeeds", metadataCopied.ok()));
    if(metadataCopied.ok())
    {
        static_cast<void>(context.expectEq("metadata copy preserves content", std::string{"abc"}, metadataCopied.value()));
    }
    const auto sourceLastWrite = FileSystem::getLastWriteTime(source);
    const auto copiedLastWrite = FileSystem::getLastWriteTime(metadataCopy);
    static_cast<void>(context.expectTrue("source last-write query succeeds", sourceLastWrite.ok()));
    static_cast<void>(context.expectTrue("metadata copy last-write query succeeds", copiedLastWrite.ok()));

    if(sourceLastWrite.ok() && copiedLastWrite.ok())
    {
        static_cast<void>(context.expectEq(
            "metadata copy preserves last-write time",
            sourceLastWrite.value().time_since_epoch().count(),
            copiedLastWrite.value().time_since_epoch().count()));
    }

    // Restore source permissions after checking copied metadata.
    const auto metadataCopyReadOnly = FileSystem::isReadOnly(metadataCopy);
    static_cast<void>(context.expectTrue("metadata copy read-only query succeeds", metadataCopyReadOnly.ok()));
    static_cast<void>(context.expectTrue("metadata copy preserves read-only state", metadataCopyReadOnly.ok() && metadataCopyReadOnly.value()));
    static_cast<void>(context.expectTrue("source read-only cleanup succeeds", FileSystem::setReadOnly(source, false).ok()));
    static_cast<void>(context.expectTrue("metadata copy read-only cleanup succeeds", FileSystem::setReadOnly(metadataCopy, false).ok()));

    static_cast<void>(context.expectTrue("movePath succeeds", FileSystem::movePath(copy, moved).ok()));
    const auto copyExists = FileSystem::exists(copy);
    const auto movedExists = FileSystem::exists(moved);
    static_cast<void>(context.expectTrue("copy source existence query succeeds", copyExists.ok()));
    static_cast<void>(context.expectFalse("movePath removes source", copyExists.ok() && copyExists.value()));
    static_cast<void>(context.expectTrue("move destination existence query succeeds", movedExists.ok()));
    static_cast<void>(context.expectTrue("movePath creates destination", movedExists.ok() && movedExists.value()));

    // Finish file checks and remove the simple directory tree.
    static_cast<void>(context.expectTrue("remove metadata copy succeeds", FileSystem::removeFile(metadataCopy).ok()));
    static_cast<void>(context.expectTrue("truncateFile succeeds", FileSystem::truncateFile(source).ok()));
    const auto truncatedSize = FileSystem::getFileSize(source);

    if(expectStatus(context, "truncateFile size query", truncatedSize))
    {
        static_cast<void>(context.expectEq("truncateFile size is zero", std::uint64_t{0}, truncatedSize.value()));
    }

    static_cast<void>(context.expectTrue("removeFile succeeds", FileSystem::removeFile(moved).ok()));
    static_cast<void>(context.expectTrue("create empty directory succeeds", FileSystem::createDirectory(emptyDirectory).ok()));
    static_cast<void>(context.expectTrue("removeEmptyDirectory succeeds", FileSystem::removeEmptyDirectory(emptyDirectory).ok()));

    static_cast<void>(context.expectTrue("create tree directory succeeds", FileSystem::createDirectories(tree / "child").ok()));
    static_cast<void>(context.expectTrue("write tree file succeeds", FileSystem::writeAllText(tree / "child" / "file.txt", "x").ok()));
    const auto removedTree = FileSystem::removeDirectoryTree(tree);
    static_cast<void>(context.expectTrue("removeDirectoryTree succeeds", removedTree.ok()));
    if(removedTree.hasValue())
    {
        static_cast<void>(context.expectEq("removeDirectoryTree removes child file child dir root", std::uint64_t{3}, removedTree.value()));
    }

    const std::filesystem::path deepTree = directory / "deep-tree";
    std::filesystem::path deepest = deepTree;
    constexpr std::uint64_t kDeepDirectoryDepth = 32;

    for(std::uint64_t index = 0; index < kDeepDirectoryDepth; ++index)
    {
        deepest /= std::format("level_{}", index);
    }
    static_cast<void>(context.expectTrue("create deep tree succeeds", FileSystem::createDirectories(deepest).ok()));
    static_cast<void>(context.expectTrue("write deep tree leaf succeeds", FileSystem::writeAllText(deepest / "leaf.txt", "x").ok()));
    const auto removedDeepTree = FileSystem::removeDirectoryTree(deepTree);
    static_cast<void>(context.expectTrue("removeDirectoryTree handles deep tree", removedDeepTree.ok()));
    static_cast<void>(context.expectTrue(
        "removeDirectoryTree deep count includes root levels and leaf",
        removedDeepTree.hasValue() && removedDeepTree.value() == kDeepDirectoryDepth + 2));

    // Check entry limits before and during recursive removal.
    const std::filesystem::path zeroLimitTree = directory / "zero-limit-tree";
    static_cast<void>(context.expectTrue("create zero-limit tree succeeds", FileSystem::createDirectories(zeroLimitTree).ok()));
    static_cast<void>(context.expectTrue("write zero-limit tree file succeeds", FileSystem::writeAllText(zeroLimitTree / "file.txt", "x").ok()));
    const auto zeroLimitRemoval = FileSystem::removeDirectoryTree(zeroLimitTree, FileSystem::Types::Directory::RemoveTreeOptions{.maxEntries = 0});
    static_cast<void>(
        context.expectEq("zero remove limit reports SizeLimitExceeded", ErrorCode::SizeLimitExceeded, zeroLimitRemoval.status().code()));
    static_cast<void>(context.expectTrue("zero remove limit removes nothing", zeroLimitRemoval.hasValue() && zeroLimitRemoval.value() == 0));
    expectPredicate(context, "zero remove limit preserves root", FileSystem::isDirectory(zeroLimitTree), true);
    static_cast<void>(context.expectTrue("zero remove limit cleanup succeeds", FileSystem::removeDirectoryTree(zeroLimitTree).ok()));

    const std::filesystem::path exactLimitTree = directory / "exact-limit-tree";
    static_cast<void>(context.expectTrue("create exact-limit tree succeeds", FileSystem::createDirectories(exactLimitTree / "child").ok()));
    static_cast<void>(
        context.expectTrue("write exact-limit tree file succeeds", FileSystem::writeAllText(exactLimitTree / "child" / "file.txt", "x").ok()));
    const auto exactLimitRemoval = FileSystem::removeDirectoryTree(exactLimitTree, FileSystem::Types::Directory::RemoveTreeOptions{.maxEntries = 3});
    static_cast<void>(context.expectTrue("exact remove limit completes tree", exactLimitRemoval.ok()));
    if(exactLimitRemoval.ok())
    {
        static_cast<void>(context.expectEq("exact remove limit counts every entry", std::uint64_t{3}, exactLimitRemoval.value()));
    }

    // Check a partial limit on a wide directory.
    const std::filesystem::path wideTree = directory / "wide-tree";
    static_cast<void>(context.expectTrue("create wide tree succeeds", FileSystem::createDirectories(wideTree).ok()));
    constexpr std::uint64_t kWideEntryCount = 128;
    bool wideWritesSucceeded = true;

    for(std::uint64_t index = 0; index < kWideEntryCount; ++index)
    {
        const bool writeSucceeded = FileSystem::writeAllText(wideTree / std::format("entry_{:03}.txt", index), "x").ok();
        wideWritesSucceeded = wideWritesSucceeded && writeSucceeded;
    }
    static_cast<void>(context.expectTrue("write wide tree files succeeds", wideWritesSucceeded));
    constexpr std::uint64_t kPartialRemovalLimit = 5;
    const auto partialRemoval =
        FileSystem::removeDirectoryTree(wideTree, FileSystem::Types::Directory::RemoveTreeOptions{.maxEntries = kPartialRemovalLimit});
    static_cast<void>(
        context.expectEq("partial tree removal reports SizeLimitExceeded", ErrorCode::SizeLimitExceeded, partialRemoval.status().code()));
    static_cast<void>(
        context.expectTrue("partial tree removal honors exact limit", partialRemoval.hasValue() && partialRemoval.value() == kPartialRemovalLimit));
    expectPredicate(context, "partial tree removal preserves root", FileSystem::isDirectory(wideTree), true);
    static_cast<void>(expectStatus(context, "partial tree removal cleanup succeeds", FileSystem::removeDirectoryTree(wideTree)));
}
