/// @file file_test.inl
/// @brief Focused filesystem file correctness suites.

/// @brief Verifies final and intermediate symlink handling for every query policy.
void testSymlinkPolicies(TestSupport::Context &context, const std::filesystem::path &root)
{
    const std::filesystem::path targetDirectory = root / "symlinks" / "target";
    const std::filesystem::path targetFile = targetDirectory / "file.txt";
    const std::filesystem::path finalFileLink = root / "symlinks" / "final-file-link";
    const std::filesystem::path intermediateDirectoryLink = root / "symlinks" / "intermediate-directory-link";

    static_cast<void>(context.expectTrue("symlink fixture directory creation succeeds", TestSupport::createDirectories(targetDirectory).ok()));
    static_cast<void>(context.expectTrue("symlink fixture file write succeeds", TestSupport::writeTextFile(targetFile, "payload").ok()));

    const auto doNotFollow = queryOptions(FileSystem::Types::SymlinkPolicy::DoNotFollow);
    const auto followFinal = queryOptions(FileSystem::Types::SymlinkPolicy::FollowFinal);
    const auto followAll = queryOptions(FileSystem::Types::SymlinkPolicy::FollowAll);

    if (createFileSymlink(context, targetFile, finalFileLink))
    {
        const auto finalNoFollow = FileSystem::isSymlink(finalFileLink, doNotFollow);
        static_cast<void>(context.expectTrue("DoNotFollow final symlink query succeeds", finalNoFollow.status.ok()));
        static_cast<void>(context.expectTrue("DoNotFollow final symlink reports symlink", finalNoFollow.value));

        const auto finalFollow = FileSystem::isRegularFile(finalFileLink, followFinal);
        static_cast<void>(context.expectTrue("FollowFinal final symlink query succeeds", finalFollow.status.ok()));
        static_cast<void>(context.expectTrue("FollowFinal final symlink reports target file", finalFollow.value));

        const auto finalFollowAll = FileSystem::isRegularFile(finalFileLink, followAll);
        static_cast<void>(context.expectTrue("FollowAll final symlink query succeeds", finalFollowAll.status.ok()));
        static_cast<void>(context.expectTrue("FollowAll final symlink reports target file", finalFollowAll.value));

        FileSystem::FileReader strictReader;
        const IO::Types::Status strictReaderOpen = strictReader.open(
            finalFileLink,
            FileSystem::Types::File::ReaderOpenOptions{.symlinkPolicy = FileSystem::Types::SymlinkPolicy::DoNotFollow});
        static_cast<void>(context.expectEq("DoNotFollow file open rejects final symlink", ErrorCode::InvalidArgument, strictReaderOpen.code));

        FileSystem::FileReader finalReader;
        const IO::Types::Status finalReaderOpen = finalReader.open(
            finalFileLink,
            FileSystem::Types::File::ReaderOpenOptions{.symlinkPolicy = FileSystem::Types::SymlinkPolicy::FollowFinal});
        static_cast<void>(context.expectTrue("FollowFinal file open reaches final symlink target", finalReaderOpen.ok()));
        static_cast<void>(context.expectTrue("FollowFinal final symlink reader closes", finalReader.close().ok()));

        const IO::Types::Status removeFinalLinkTarget =
            FileSystem::removeFile(finalFileLink, FileSystem::Types::RemoveOptions{.symlinkPolicy = FileSystem::Types::SymlinkPolicy::FollowFinal});
        static_cast<void>(context.expectEq("FollowFinal remove final symlink is unsupported", ErrorCode::Unsupported, removeFinalLinkTarget.code));

        const IO::Types::Status moveFinalLinkTarget = FileSystem::movePath(
            finalFileLink,
            root / "symlinks" / "moved-through-final-link.txt",
            FileSystem::Types::MoveOptions{.symlinkPolicy = FileSystem::Types::SymlinkPolicy::FollowFinal});
        static_cast<void>(context.expectEq("FollowFinal move final symlink is unsupported", ErrorCode::Unsupported, moveFinalLinkTarget.code));

        const auto finalLinkStillExists = FileSystem::isSymlink(finalFileLink);
        static_cast<void>(context.expectTrue("unsupported final symlink mutation preserves link query", finalLinkStillExists.status.ok()));
        static_cast<void>(context.expectTrue("unsupported final symlink mutation preserves link", finalLinkStillExists.value));
    }

    if (createDirectorySymlink(context, targetDirectory, intermediateDirectoryLink))
    {
        const std::filesystem::path pathThroughIntermediate = intermediateDirectoryLink / "file.txt";

        const auto followAllThroughIntermediate = FileSystem::isRegularFile(pathThroughIntermediate, followAll);
        static_cast<void>(context.expectTrue("FollowAll allows intermediate symlink", followAllThroughIntermediate.status.ok()));
        static_cast<void>(context.expectTrue("FollowAll reaches file through intermediate symlink", followAllThroughIntermediate.value));

        const auto noFollowThroughIntermediate = FileSystem::isRegularFile(pathThroughIntermediate, doNotFollow);
        static_cast<void>(
            context.expectEq("DoNotFollow rejects intermediate symlink", ErrorCode::PermissionDenied, noFollowThroughIntermediate.status.code));

        const auto followFinalThroughIntermediate = FileSystem::isRegularFile(pathThroughIntermediate, followFinal);
        static_cast<void>(
            context.expectEq("FollowFinal rejects intermediate symlink", ErrorCode::PermissionDenied, followFinalThroughIntermediate.status.code));

        const IO::Types::WriteResult strictWrite = FileSystem::writeAllText(
            pathThroughIntermediate,
            "blocked",
            FileSystem::Types::File::WriteOptions{.symlinkPolicy = FileSystem::Types::SymlinkPolicy::DoNotFollow});
        static_cast<void>(context.expectEq("DoNotFollow write rejects intermediate symlink", ErrorCode::PermissionDenied, strictWrite.status.code));

        const auto strictListing = FileSystem::listDirectory(
            intermediateDirectoryLink,
            FileSystem::Types::Directory::ListOptions{.symlinkPolicy = FileSystem::Types::SymlinkPolicy::DoNotFollow});
        static_cast<void>(context.expectEq("DoNotFollow listDirectory rejects symlink root", ErrorCode::NotDirectory, strictListing.status.code));
    }
}

/// @brief Verifies strict UTF-8 content helpers and pre-side-effect text validation.
void testTextContracts(TestSupport::Context &context, const std::filesystem::path &root)
{
    const std::filesystem::path directory = root / "text-contracts";
    const std::filesystem::path file = directory / "content.txt";
    static_cast<void>(context.expectTrue("text contract fixture write succeeds", FileSystem::writeAllText(file, "preserved").status.ok()));

    std::string malformed{"prefix "};
    malformed.push_back(static_cast<char>(0xE2));
    malformed.push_back(static_cast<char>(0x28));
    malformed.push_back(static_cast<char>(0xA1));

    const std::filesystem::path malformedFile = directory / "malformed.bin";
    const auto rawWrite = FileSystem::writeAllBytes(malformedFile, bytesOf(malformed));
    static_cast<void>(context.expectTrue("byte helper writes malformed fixture bytes", rawWrite.status.ok()));
    const auto malformedRead = FileSystem::readAllText(malformedFile);
    static_cast<void>(context.expectEq("readAllText rejects malformed UTF-8", ErrorCode::EncodingFailed, malformedRead.status.code));
    static_cast<void>(context.expectEq("readAllText preserves valid prefix", std::string{"prefix "}, malformedRead.text));

    std::string validText{"A\0", 2};
    validText.append("\xF0\x9F\x98\x80", 4);
    const auto validWrite = FileSystem::writeAllText(file, validText);
    static_cast<void>(context.expectTrue("writeAllText accepts embedded NUL and multibyte UTF-8", validWrite.status.ok()));
    const auto validRead = FileSystem::readAllText(file);
    static_cast<void>(context.expectTrue("readAllText returns valid multibyte UTF-8", validRead.status.ok()));
    static_cast<void>(context.expectEq("valid text round-trips exactly", validText, validRead.text));

    static_cast<void>(context.expectTrue("restore preservation fixture", FileSystem::writeAllText(file, "preserved").status.ok()));
    const auto invalidWrite = FileSystem::writeAllText(file, malformed);
    static_cast<void>(context.expectEq("writeAllText rejects malformed UTF-8", ErrorCode::EncodingFailed, invalidWrite.status.code));
    static_cast<void>(context.expectEq("invalid write reports zero progress", std::size_t{0}, invalidWrite.bytesWritten));
    static_cast<void>(context.expectEq("invalid write preserves existing content", std::string{"preserved"}, FileSystem::readAllText(file).text));

    const std::filesystem::path missingWrite = root / "invalid-write-parent" / "file.txt";
    const auto missingInvalidWrite = FileSystem::writeAllText(missingWrite, malformed);
    static_cast<void>(
        context.expectEq("invalid write to missing path is EncodingFailed", ErrorCode::EncodingFailed, missingInvalidWrite.status.code));
    static_cast<void>(context.expectFalse("invalid write creates no parent", FileSystem::exists(missingWrite.parent_path()).value));

    const auto invalidAppend = FileSystem::appendText(file, malformed);
    static_cast<void>(context.expectEq("appendText rejects malformed UTF-8", ErrorCode::EncodingFailed, invalidAppend.status.code));
    static_cast<void>(context.expectEq("invalid append reports zero progress", std::size_t{0}, invalidAppend.bytesWritten));
    static_cast<void>(context.expectEq("invalid append preserves content", std::string{"preserved"}, FileSystem::readAllText(file).text));

    const std::filesystem::path missingAppend = root / "invalid-append-parent" / "file.txt";
    const auto missingInvalidAppend = FileSystem::appendText(missingAppend, malformed);
    static_cast<void>(
        context.expectEq("invalid append to missing path is EncodingFailed", ErrorCode::EncodingFailed, missingInvalidAppend.status.code));
    static_cast<void>(context.expectFalse("invalid append creates no parent", FileSystem::exists(missingAppend.parent_path()).value));

    const auto invalidAtomic = FileSystem::writeAllTextAtomic(file, malformed);
    static_cast<void>(context.expectEq("writeAllTextAtomic rejects malformed UTF-8", ErrorCode::EncodingFailed, invalidAtomic.code));
    static_cast<void>(context.expectEq("invalid atomic write preserves destination", std::string{"preserved"}, FileSystem::readAllText(file).text));

    const std::filesystem::path missingAtomic = root / "invalid-atomic-parent" / "file.txt";
    const auto missingInvalidAtomic = FileSystem::writeAllTextAtomic(missingAtomic, malformed);
    static_cast<void>(
        context.expectEq("invalid atomic write to missing path is EncodingFailed", ErrorCode::EncodingFailed, missingInvalidAtomic.code));
    static_cast<void>(context.expectFalse("invalid atomic write creates no parent", FileSystem::exists(missingAtomic.parent_path()).value));

    FileSystem::Types::File::AtomicWriteOptions unicodePrefixOptions;
    unicodePrefixOptions.temporaryNamePrefix = "tmp_\xD1\x84_";
    const auto unicodePrefixWrite = FileSystem::writeAllTextAtomic(file, "unicode-prefix", unicodePrefixOptions);
    static_cast<void>(context.expectTrue("atomic write accepts UTF-8 temporary prefix", unicodePrefixWrite.ok()));

    FileSystem::Types::File::AtomicWriteOptions malformedPrefixOptions;
    malformedPrefixOptions.temporaryNamePrefix.assign(1, static_cast<char>(0xFF));
    const auto malformedPrefixWrite = FileSystem::writeAllTextAtomic(file, "must-not-write", malformedPrefixOptions);
    static_cast<void>(
        context.expectEq("atomic write rejects malformed UTF-8 temporary prefix", ErrorCode::EncodingFailed, malformedPrefixWrite.code));
    static_cast<void>(
        context.expectEq("invalid temporary prefix preserves destination", std::string{"unicode-prefix"}, FileSystem::readAllText(file).text));
}

/// @brief Verifies whole-file helpers and reader, writer, and read/write handle contracts.
void testWholeFileHelpersAndHandles(TestSupport::Context &context, const std::filesystem::path &root)
{
    const std::filesystem::path file = root / "handles" / "stream.txt";

    const IO::Types::WriteResult write = FileSystem::writeAllText(file, "abc");
    static_cast<void>(context.expectTrue("writeAllText succeeds", write.status.ok()));
    static_cast<void>(context.expectEq("writeAllText byte count", std::size_t{3}, write.bytesWritten));

    const IO::Types::WriteResult append = FileSystem::appendText(file, "def");
    static_cast<void>(context.expectTrue("appendText succeeds", append.status.ok()));
    static_cast<void>(context.expectEq("appendText byte count", std::size_t{3}, append.bytesWritten));

    const IO::Types::ReadAllTextResult text = FileSystem::readAllText(file);
    static_cast<void>(context.expectTrue("readAllText succeeds", text.status.ok()));
    static_cast<void>(context.expectEq("readAllText sees appended content", std::string{"abcdef"}, text.text));

    FileSystem::File fileHandle;
    const IO::Types::Status openStatus = fileHandle.open(
        file,
        FileSystem::Types::File::OpenOptions{
            .access = FileSystem::Types::File::Access::ReadWrite,
            .mode = FileSystem::Types::File::OpenMode::OpenExisting,
            .initialPosition = FileSystem::Types::File::InitialPosition::End});
    static_cast<void>(context.expectTrue("File open succeeds", openStatus.ok()));

    const IO::Types::PositionResult endPosition = fileHandle.position();
    static_cast<void>(context.expectTrue("File position succeeds", endPosition.status.ok()));
    static_cast<void>(context.expectEq("File::InitialPosition::End seeks to EOF", std::uint64_t{6}, endPosition.position));

    const IO::Types::WriteResult handleWrite = fileHandle.write(bytesOf("g"));
    static_cast<void>(context.expectTrue("File write succeeds", handleWrite.status.ok()));
    static_cast<void>(context.expectEq("File write byte count", std::size_t{1}, handleWrite.bytesWritten));
    static_cast<void>(context.expectTrue("File close succeeds", fileHandle.close().ok()));

    const IO::Types::ReadAllTextResult updated = FileSystem::readAllText(file);
    static_cast<void>(context.expectTrue("readAllText after File write succeeds", updated.status.ok()));
    static_cast<void>(context.expectEq("File write updated content", std::string{"abcdefg"}, updated.text));

    FileSystem::FileWriter appendWriter;
    const IO::Types::Status appendOpen =
        appendWriter.open(file, FileSystem::Types::File::WriterOpenOptions{.mode = FileSystem::Types::File::WriterMode::AppendExisting});
    static_cast<void>(context.expectTrue("append writer opens", appendOpen.ok()));
    static_cast<void>(context.expectFalse("append writer is not seekable", appendWriter.canSeek()));
    static_cast<void>(context.expectEq("append writer position is NotSeekable", ErrorCode::NotSeekable, appendWriter.position().status.code));
    static_cast<void>(
        context.expectEq("append writer seek is NotSeekable", ErrorCode::NotSeekable, appendWriter.seek(0, IO::Types::SeekOrigin::Begin).code));
    static_cast<void>(context.expectTrue("append writer writes", appendWriter.write(bytesOf("h")).status.ok()));
    static_cast<void>(context.expectTrue("append writer closes", appendWriter.close().ok()));

    const IO::Types::ReadAllTextResult appendUpdated = FileSystem::readAllText(file);
    static_cast<void>(context.expectTrue("readAllText after append writer succeeds", appendUpdated.status.ok()));
    static_cast<void>(context.expectEq("append writer adds content at EOF", std::string{"abcdefgh"}, appendUpdated.text));
}

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
    static_cast<void>(context.expectTrue("write mutation source", FileSystem::writeAllText(source, "abcdef").status.ok()));

    static_cast<void>(context.expectTrue("resizeFile succeeds", FileSystem::resizeFile(source, 3).ok()));
    const IO::Types::ReadAllTextResult resized = FileSystem::readAllText(source);
    static_cast<void>(context.expectTrue("read resized file succeeds", resized.status.ok()));
    static_cast<void>(context.expectEq("resizeFile truncates content", std::string{"abc"}, resized.text));

    static_cast<void>(context.expectTrue("copyFile succeeds", FileSystem::copyFile(source, copy).ok()));
    const IO::Types::ReadAllTextResult copied = FileSystem::readAllText(copy);
    static_cast<void>(context.expectTrue("read copied file succeeds", copied.status.ok()));
    static_cast<void>(context.expectEq("copyFile preserves content", std::string{"abc"}, copied.text));

    static_cast<void>(context.expectTrue("source read-only setup succeeds", FileSystem::setReadOnly(source, true).ok()));
    FileSystem::Types::File::CopyOptions copyOptions;
    copyOptions.metadataMode = FileSystem::Types::File::CopyMetadataMode::Basic;
    static_cast<void>(context.expectTrue("copyFile basic metadata succeeds", FileSystem::copyFile(source, metadataCopy, copyOptions).ok()));
    const IO::Types::ReadAllTextResult metadataCopied = FileSystem::readAllText(metadataCopy);
    static_cast<void>(context.expectTrue("read metadata copy succeeds", metadataCopied.status.ok()));
    static_cast<void>(context.expectEq("metadata copy preserves content", std::string{"abc"}, metadataCopied.text));
    const auto sourceLastWrite = FileSystem::getLastWriteTime(source);
    const auto copiedLastWrite = FileSystem::getLastWriteTime(metadataCopy);
    static_cast<void>(context.expectTrue("source last-write query succeeds", sourceLastWrite.status.ok()));
    static_cast<void>(context.expectTrue("metadata copy last-write query succeeds", copiedLastWrite.status.ok()));
    if (sourceLastWrite.status.ok() && copiedLastWrite.status.ok())
    {
        static_cast<void>(context.expectEq(
            "metadata copy preserves last-write time",
            sourceLastWrite.time.time_since_epoch().count(),
            copiedLastWrite.time.time_since_epoch().count()));
    }
    const auto metadataCopyReadOnly = FileSystem::isReadOnly(metadataCopy);
    static_cast<void>(context.expectTrue("metadata copy read-only query succeeds", metadataCopyReadOnly.status.ok()));
    static_cast<void>(context.expectTrue("metadata copy preserves read-only state", metadataCopyReadOnly.value));
    static_cast<void>(context.expectTrue("source read-only cleanup succeeds", FileSystem::setReadOnly(source, false).ok()));
    static_cast<void>(context.expectTrue("metadata copy read-only cleanup succeeds", FileSystem::setReadOnly(metadataCopy, false).ok()));

    static_cast<void>(context.expectTrue("movePath succeeds", FileSystem::movePath(copy, moved).ok()));
    const auto copyExists = FileSystem::exists(copy);
    const auto movedExists = FileSystem::exists(moved);
    static_cast<void>(context.expectTrue("copy source existence query succeeds", copyExists.status.ok()));
    static_cast<void>(context.expectFalse("movePath removes source", copyExists.value));
    static_cast<void>(context.expectTrue("move destination existence query succeeds", movedExists.status.ok()));
    static_cast<void>(context.expectTrue("movePath creates destination", movedExists.value));

#if defined(_WIN32) && FILESYSTEM_INTERNAL_TEST_HOOKS
    {
        const std::filesystem::path raceSource = directory / "move-race-source.txt";
        const std::filesystem::path validatedParent = directory / "validated-parent";
        const std::filesystem::path parkedParent = directory / "validated-parent-parked";
        const std::filesystem::path destination = validatedParent / "destination.txt";
        static_cast<void>(context.expectTrue("write move race source", FileSystem::writeAllText(raceSource, "anchored").status.ok()));
        static_cast<void>(context.expectTrue("create validated move parent", FileSystem::createDirectory(validatedParent).ok()));

        FileSystem::Detail::Platform::TestHooks::armMoveDestinationValidatedPause();
        IO::Types::Status moveRaceStatus;
        std::thread moveThread(
            [&]
            {
                moveRaceStatus = FileSystem::movePath(raceSource, destination);
            });
        const bool movePaused = FileSystem::Detail::Platform::TestHooks::waitForMovePause(std::chrono::seconds{5});
        static_cast<void>(context.expectTrue("strict move reaches destination validation pause", movePaused));
        if (movePaused)
        {
            std::error_code renameError;
            std::filesystem::rename(validatedParent, parkedParent, renameError);
            static_cast<void>(context.expectFalse("validated parent can be renamed during pause", static_cast<bool>(renameError)));
            static_cast<void>(context.expectTrue("create replacement move parent", FileSystem::createDirectory(validatedParent).ok()));
        }
        FileSystem::Detail::Platform::TestHooks::releaseMovePause();
        moveThread.join();

        static_cast<void>(context.expectTrue("anchored strict move succeeds", moveRaceStatus.ok()));
        static_cast<void>(context.expectTrue("anchored move lands in retained parent", FileSystem::exists(parkedParent / "destination.txt").value));
        static_cast<void>(context.expectFalse("anchored move avoids replacement parent", FileSystem::exists(destination).value));
        FileSystem::Detail::Platform::TestHooks::reset();
    }

    {
        const std::filesystem::path commitSource = directory / "commit-source.txt";
        const std::filesystem::path commitDestination = directory / "commit-destination.txt";
        static_cast<void>(context.expectTrue("write post-commit source", FileSystem::writeAllText(commitSource, "original").status.ok()));

        FileSystem::Detail::Platform::TestHooks::armMoveCommittedPause();
        IO::Types::Status committedStatus;
        std::thread moveThread(
            [&]
            {
                committedStatus = FileSystem::movePath(commitSource, commitDestination);
            });
        const bool movePaused = FileSystem::Detail::Platform::TestHooks::waitForMovePause(std::chrono::seconds{5});
        static_cast<void>(context.expectTrue("move reaches committed pause", movePaused));
        if (movePaused)
        {
            static_cast<void>(
                context.expectTrue("source can be recreated after commit", FileSystem::writeAllText(commitSource, "replacement").status.ok()));
            static_cast<void>(context.expectTrue("destination can be removed after commit", FileSystem::removeFile(commitDestination).ok()));
        }
        FileSystem::Detail::Platform::TestHooks::releaseMovePause();
        moveThread.join();

        static_cast<void>(context.expectTrue("native commit remains successful after namespace mutation", committedStatus.ok()));
        static_cast<void>(
            context.expectEq("recreated source remains caller-visible", std::string{"replacement"}, FileSystem::readAllText(commitSource).text));
        static_cast<void>(context.expectFalse("concurrently removed destination remains absent", FileSystem::exists(commitDestination).value));
        FileSystem::Detail::Platform::TestHooks::reset();
    }
#endif

    static_cast<void>(context.expectTrue("remove metadata copy succeeds", FileSystem::removeFile(metadataCopy).ok()));
    static_cast<void>(context.expectTrue("truncateFile succeeds", FileSystem::truncateFile(source).ok()));
    static_cast<void>(context.expectEq("truncateFile size is zero", std::uint64_t{0}, FileSystem::getFileSize(source).sizeBytes));

    static_cast<void>(context.expectTrue("removeFile succeeds", FileSystem::removeFile(moved).ok()));
    static_cast<void>(context.expectTrue("create empty directory succeeds", FileSystem::createDirectory(emptyDirectory).ok()));
    static_cast<void>(context.expectTrue("removeEmptyDirectory succeeds", FileSystem::removeEmptyDirectory(emptyDirectory).ok()));

    static_cast<void>(context.expectTrue("create tree directory succeeds", FileSystem::createDirectories(tree / "child").ok()));
    static_cast<void>(context.expectTrue("write tree file succeeds", FileSystem::writeAllText(tree / "child" / "file.txt", "x").status.ok()));
    const auto removedTree = FileSystem::removeDirectoryTree(tree);
    static_cast<void>(context.expectTrue("removeDirectoryTree succeeds", removedTree.status.ok()));
    static_cast<void>(context.expectEq("removeDirectoryTree removes child file child dir root", std::uint64_t{3}, removedTree.removedEntries));

    const std::filesystem::path deepTree = directory / "deep-tree";
    std::filesystem::path deepest = deepTree;
    constexpr std::uint64_t kDeepDirectoryDepth = 32;
    for (std::uint64_t index = 0; index < kDeepDirectoryDepth; ++index)
    {
        deepest /= std::format("level_{}", index);
    }
    static_cast<void>(context.expectTrue("create deep tree succeeds", FileSystem::createDirectories(deepest).ok()));
    static_cast<void>(context.expectTrue("write deep tree leaf succeeds", FileSystem::writeAllText(deepest / "leaf.txt", "x").status.ok()));
    const auto removedDeepTree = FileSystem::removeDirectoryTree(deepTree);
    static_cast<void>(context.expectTrue("removeDirectoryTree handles deep tree", removedDeepTree.status.ok()));
    static_cast<void>(
        context.expectEq("removeDirectoryTree deep count includes root levels and leaf", kDeepDirectoryDepth + 2, removedDeepTree.removedEntries));

    const std::filesystem::path zeroLimitTree = directory / "zero-limit-tree";
    static_cast<void>(context.expectTrue("create zero-limit tree succeeds", FileSystem::createDirectories(zeroLimitTree).ok()));
    static_cast<void>(
        context.expectTrue("write zero-limit tree file succeeds", FileSystem::writeAllText(zeroLimitTree / "file.txt", "x").status.ok()));
    const auto zeroLimitRemoval = FileSystem::removeDirectoryTree(zeroLimitTree, FileSystem::Types::Directory::RemoveTreeOptions{.maxEntries = 0});
    static_cast<void>(context.expectEq("zero remove limit reports SizeLimitExceeded", ErrorCode::SizeLimitExceeded, zeroLimitRemoval.status.code));
    static_cast<void>(context.expectEq("zero remove limit removes nothing", std::uint64_t{0}, zeroLimitRemoval.removedEntries));
    static_cast<void>(context.expectTrue("zero remove limit preserves root", FileSystem::isDirectory(zeroLimitTree).value));
    static_cast<void>(context.expectTrue("zero remove limit cleanup succeeds", FileSystem::removeDirectoryTree(zeroLimitTree).status.ok()));

    const std::filesystem::path exactLimitTree = directory / "exact-limit-tree";
    static_cast<void>(context.expectTrue("create exact-limit tree succeeds", FileSystem::createDirectories(exactLimitTree / "child").ok()));
    static_cast<void>(
        context.expectTrue("write exact-limit tree file succeeds", FileSystem::writeAllText(exactLimitTree / "child" / "file.txt", "x").status.ok()));
    const auto exactLimitRemoval = FileSystem::removeDirectoryTree(exactLimitTree, FileSystem::Types::Directory::RemoveTreeOptions{.maxEntries = 3});
    static_cast<void>(context.expectTrue("exact remove limit completes tree", exactLimitRemoval.status.ok()));
    static_cast<void>(context.expectEq("exact remove limit counts every entry", std::uint64_t{3}, exactLimitRemoval.removedEntries));

    const std::filesystem::path wideTree = directory / "wide-tree";
    static_cast<void>(context.expectTrue("create wide tree succeeds", FileSystem::createDirectories(wideTree).ok()));
    constexpr std::uint64_t kWideEntryCount = 128;
    bool wideWritesSucceeded = true;
    for (std::uint64_t index = 0; index < kWideEntryCount; ++index)
    {
        const bool writeSucceeded = FileSystem::writeAllText(wideTree / std::format("entry_{:03}.txt", index), "x").status.ok();
        wideWritesSucceeded = wideWritesSucceeded && writeSucceeded;
    }
    static_cast<void>(context.expectTrue("write wide tree files succeeds", wideWritesSucceeded));
    constexpr std::uint64_t kPartialRemovalLimit = 5;
    const auto partialRemoval =
        FileSystem::removeDirectoryTree(wideTree, FileSystem::Types::Directory::RemoveTreeOptions{.maxEntries = kPartialRemovalLimit});
    static_cast<void>(context.expectEq("partial tree removal reports SizeLimitExceeded", ErrorCode::SizeLimitExceeded, partialRemoval.status.code));
    static_cast<void>(context.expectEq("partial tree removal honors exact limit", kPartialRemovalLimit, partialRemoval.removedEntries));
    static_cast<void>(context.expectTrue("partial tree removal preserves root", FileSystem::isDirectory(wideTree).value));
    static_cast<void>(context.expectTrue("partial tree removal cleanup succeeds", FileSystem::removeDirectoryTree(wideTree).status.ok()));
}

/// @brief Verifies atomic replacement and non-blocking whole-file lock ownership.
void testAtomicWriteAndLocks(TestSupport::Context &context, const std::filesystem::path &root)
{
    const std::filesystem::path file = root / "atomic-locks" / "file.txt";

    static_cast<void>(context.expectTrue("initial atomic test write succeeds", FileSystem::writeAllText(file, "old").status.ok()));
    const IO::Types::Status atomicReplace = FileSystem::writeAllTextAtomic(file, "new");
    static_cast<void>(context.expectTrue("writeAllTextAtomic replace succeeds", atomicReplace.ok()));

    const IO::Types::ReadAllTextResult replaced = FileSystem::readAllText(file);
    static_cast<void>(context.expectTrue("read atomic replacement succeeds", replaced.status.ok()));
    static_cast<void>(context.expectEq("atomic replacement content", std::string{"new"}, replaced.text));

    const IO::Types::Status atomicFailIfExists = FileSystem::writeAllTextAtomic(
        file,
        "bad",
        FileSystem::Types::File::AtomicWriteOptions{.replaceMode = FileSystem::Types::ReplaceMode::FailIfExists});
    static_cast<void>(context.expectEq("atomic fail-if-exists reports AlreadyExists", ErrorCode::AlreadyExists, atomicFailIfExists.code));
    static_cast<void>(context.expectEq("atomic fail-if-exists preserves content", std::string{"new"}, FileSystem::readAllText(file).text));

    FileSystem::File lockFile;
    const IO::Types::Status openStatus = lockFile.open(
        file,
        FileSystem::Types::File::OpenOptions{
            .access = FileSystem::Types::File::Access::ReadWrite,
            .mode = FileSystem::Types::File::OpenMode::OpenExisting});
    static_cast<void>(context.expectTrue("lock file opens", openStatus.ok()));

    auto lock = lockFile.tryLockExclusive();
    static_cast<void>(context.expectTrue("exclusive lock call succeeds", lock.status.ok()));
    static_cast<void>(context.expectEq("exclusive lock acquired", FileSystem::Types::Lock::Outcome::Acquired, lock.outcome));

    FileSystem::File competingLockFile;
    const IO::Types::Status competingOpenStatus = competingLockFile.open(
        file,
        FileSystem::Types::File::OpenOptions{
            .access = FileSystem::Types::File::Access::ReadWrite,
            .mode = FileSystem::Types::File::OpenMode::OpenExisting});
    static_cast<void>(context.expectTrue("competing lock file opens", competingOpenStatus.ok()));
    auto competingLock = competingLockFile.tryLockExclusive();
    static_cast<void>(context.expectTrue("competing exclusive lock call succeeds", competingLock.status.ok()));
    static_cast<void>(context.expectEq("competing exclusive lock would block", FileSystem::Types::Lock::Outcome::WouldBlock, competingLock.outcome));
    static_cast<void>(context.expectTrue("competing lock file closes", competingLockFile.close().ok()));

    static_cast<void>(context.expectEq("close while locked reports ResourceBusy", ErrorCode::ResourceBusy, lockFile.close().code));
    static_cast<void>(context.expectTrue("unlock succeeds", lock.lock.unlock().ok()));
    static_cast<void>(context.expectTrue("close after unlock succeeds", lockFile.close().ok()));

    const std::filesystem::path detachedOwnerFile = root / "atomic-locks" / "detached-owner.txt";
    static_cast<void>(context.expectTrue("detached-owner file write succeeds", FileSystem::writeAllText(detachedOwnerFile, "lock").status.ok()));
    FileSystem::FileLock detachedOwnerLock = [&]()
    {
        FileSystem::File owner;
        static_cast<void>(context.expectTrue("detached lock owner opens", owner.open(detachedOwnerFile).ok()));
        auto result = owner.tryLockExclusive();
        static_cast<void>(context.expectEq("detached owner acquires lock", FileSystem::Types::Lock::Outcome::Acquired, result.outcome));
        return std::move(result.lock);
    }();

    FileSystem::File detachedCompetitor;
    static_cast<void>(context.expectTrue("detached lock competitor opens", detachedCompetitor.open(detachedOwnerFile).ok()));
    auto blockedByDetachedOwner = detachedCompetitor.tryLockExclusive();
    static_cast<void>(context.expectEq(
        "lock remains active after source handle destruction",
        FileSystem::Types::Lock::Outcome::WouldBlock,
        blockedByDetachedOwner.outcome));
    static_cast<void>(context.expectTrue("detached owner unlock succeeds", detachedOwnerLock.unlock().ok()));
    auto acquiredAfterDetachedUnlock = detachedCompetitor.tryLockExclusive();
    static_cast<void>(context.expectEq(
        "competitor acquires after detached owner unlock",
        FileSystem::Types::Lock::Outcome::Acquired,
        acquiredAfterDetachedUnlock.outcome));
    static_cast<void>(context.expectTrue("detached competitor unlock succeeds", acquiredAfterDetachedUnlock.lock.unlock().ok()));
    static_cast<void>(context.expectTrue("detached competitor closes", detachedCompetitor.close().ok()));

#if FILESYSTEM_INTERNAL_TEST_HOOKS
    const std::filesystem::path failedUnlockFile = root / "atomic-locks" / "failed-unlock.txt";
    static_cast<void>(context.expectTrue("failed-unlock file write succeeds", FileSystem::writeAllText(failedUnlockFile, "lock").status.ok()));

    FileSystem::File failedUnlockOwner;
    FileSystem::File failedUnlockCompetitor;
    static_cast<void>(context.expectTrue("failed-unlock owner opens", failedUnlockOwner.open(failedUnlockFile).ok()));
    static_cast<void>(context.expectTrue("failed-unlock competitor opens", failedUnlockCompetitor.open(failedUnlockFile).ok()));

    {
        auto failedUnlockLock = failedUnlockOwner.tryLockExclusive();
        static_cast<void>(
            context.expectEq("failed-unlock owner acquires lock", FileSystem::Types::Lock::Outcome::Acquired, failedUnlockLock.outcome));

        auto blockedBeforeCleanup = failedUnlockCompetitor.tryLockExclusive();
        static_cast<void>(context.expectEq(
            "failed-unlock competitor initially blocks",
            FileSystem::Types::Lock::Outcome::WouldBlock,
            blockedBeforeCleanup.outcome));

        FileSystem::Detail::Platform::TestHooks::setFileUnlockFailure(true);
        FileSystem::FileLock lockDestroyedDuringFailure = std::move(failedUnlockLock.lock);
        static_cast<void>(context.expectTrue("failed-unlock injected lock remains active", lockDestroyedDuringFailure.isActive()));
    }
    FileSystem::Detail::Platform::TestHooks::reset();

    static_cast<void>(context.expectTrue("failed-unlock owner count clears after lock destruction", failedUnlockOwner.close().ok()));
    auto acquiredAfterFailedUnlockCleanup = failedUnlockCompetitor.tryLockExclusive();
    static_cast<void>(context.expectEq(
        "closing owner releases native lock after failed lock cleanup",
        FileSystem::Types::Lock::Outcome::Acquired,
        acquiredAfterFailedUnlockCleanup.outcome));
    static_cast<void>(context.expectTrue("failed-unlock competitor releases recovered lock", acquiredAfterFailedUnlockCleanup.lock.unlock().ok()));
    static_cast<void>(context.expectTrue("failed-unlock competitor closes", failedUnlockCompetitor.close().ok()));
#endif
}

#if FILESYSTEM_INTERNAL_TEST_HOOKS
/// @brief Verifies checked file operations translate exceptions and retain retryable close state.
void testCheckedFileFailureTranslation(TestSupport::Context &context, const std::filesystem::path &root)
{
    namespace Hooks = FileSystem::Detail::Platform::TestHooks;
    using Operation = Hooks::CheckedFileOperation;
    using Failure = Hooks::CheckedFailure;

    const std::filesystem::path filePath = root / "checked_failures" / "stream.txt";
    static_cast<void>(context.expectTrue("checked failure fixture write succeeds", FileSystem::writeAllText(filePath, "payload").status.ok()));

    FileSystem::File file;
    const IO::Types::Status openStatus = file.open(
        filePath,
        FileSystem::Types::File::OpenOptions{
            .access = FileSystem::Types::File::Access::ReadWrite,
            .mode = FileSystem::Types::File::OpenMode::OpenExisting,
            .flushOnClose = IO::Types::FlushMode::None});
    static_cast<void>(context.expectTrue("checked failure fixture opens", openStatus.ok()));

    auto verifyTranslations = [&context](Operation operation, std::string_view label, auto &&invoke)
    {
        Hooks::forceNextCheckedFailure(operation, Failure::Status, ErrorCode::PermissionDenied, 1977);
        IO::Types::Status status = std::invoke(invoke);
        static_cast<void>(context.expectEq(std::format("{} preserves injected status", label), ErrorCode::PermissionDenied, status.code));
        static_cast<void>(context.expectEq(std::format("{} preserves injected native code", label), std::int64_t{1977}, status.nativeCode));

        Hooks::forceNextCheckedFailure(operation, Failure::OutOfMemory);
        status = std::invoke(invoke);
        static_cast<void>(context.expectEq(std::format("{} translates allocation failure", label), ErrorCode::OutOfMemory, status.code));

        Hooks::forceNextCheckedFailure(operation, Failure::Unexpected);
        status = std::invoke(invoke);
        static_cast<void>(context.expectEq(std::format("{} translates unexpected failure", label), ErrorCode::Unknown, status.code));
    };

    std::byte byte{};
    verifyTranslations(
        Operation::Read,
        "File read",
        [&file, &byte]
        {
            IO::Types::ReadResult result = file.read(std::span<std::byte>{&byte, 1});
            return std::move(result.status);
        });
    verifyTranslations(
        Operation::Write,
        "File write",
        [&file, &byte]
        {
            IO::Types::WriteResult result = file.write(std::span<const std::byte>{&byte, 1});
            return std::move(result.status);
        });
    verifyTranslations(
        Operation::Flush,
        "File flush",
        [&file]
        {
            return file.flush(IO::Types::FlushMode::None);
        });
    verifyTranslations(
        Operation::Position,
        "File position",
        [&file]
        {
            IO::Types::PositionResult result = file.position();
            return std::move(result.status);
        });
    verifyTranslations(
        Operation::Size,
        "File size",
        [&file]
        {
            IO::Types::SizeResult result = file.size();
            return std::move(result.status);
        });
    verifyTranslations(
        Operation::Seek,
        "File seek",
        [&file]
        {
            return file.seek(0, IO::Types::SeekOrigin::Begin);
        });
    verifyTranslations(
        Operation::Resize,
        "File resize",
        [&file]
        {
            return file.resize(7);
        });
    verifyTranslations(
        Operation::Close,
        "File close",
        [&file]
        {
            return file.close();
        });
    static_cast<void>(context.expectTrue("failed checked closes retain open state", file.isOpen()));
    static_cast<void>(context.expectTrue("checked close can be retried", file.close().ok()));

    Hooks::reset();

    FileSystem::File flushOnCloseFile;
    const IO::Types::Status flushOnCloseOpenStatus = flushOnCloseFile.open(
        filePath,
        FileSystem::Types::File::OpenOptions{
            .access = FileSystem::Types::File::Access::ReadWrite,
            .mode = FileSystem::Types::File::OpenMode::OpenExisting,
            .flushOnClose = IO::Types::FlushMode::Data});
    static_cast<void>(context.expectTrue("flush-on-close fixture opens", flushOnCloseOpenStatus.ok()));

    Hooks::forceNextCheckedFailure(Operation::Flush, Failure::Status, ErrorCode::FlushFailed, 1978);
    const IO::Types::Status failedFlushOnCloseStatus = flushOnCloseFile.close();
    static_cast<void>(context.expectEq("flush-on-close preserves injected status", ErrorCode::FlushFailed, failedFlushOnCloseStatus.code));
    static_cast<void>(context.expectEq("flush-on-close preserves injected native code", std::int64_t{1978}, failedFlushOnCloseStatus.nativeCode));
    static_cast<void>(context.expectTrue("flush-on-close failure retains open state", flushOnCloseFile.isOpen()));

    const IO::Types::Status flushOnCloseRetryStatus = flushOnCloseFile.close();
    static_cast<void>(context.expectTrue("flush-on-close retry succeeds", flushOnCloseRetryStatus.ok()));
    static_cast<void>(context.expectTrue("flush-on-close retry closes state", !flushOnCloseFile.isOpen()));

    Hooks::reset();

    const std::filesystem::path missingPath = root / "checked_failures" / "missing.txt";
    for (const Failure failure : {Failure::OutOfMemory, Failure::Unexpected})
    {
        Hooks::forceNextCheckedFailure(Operation::DiagnosticMessage, failure);
        FileSystem::FileReader missingReader;
        IO::Types::Status missingStatus = missingReader.open(missingPath);
        static_cast<void>(context.expectEq("diagnostic failure preserves portable error", ErrorCode::NotFound, missingStatus.code));
        static_cast<void>(context.expectTrue("diagnostic failure preserves native error", missingStatus.nativeCode != 0));
        static_cast<void>(context.expectTrue("diagnostic failure falls back to an empty message", missingStatus.message.empty()));
    }

    Hooks::reset();
}
#endif
