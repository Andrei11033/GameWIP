/// @file symlink_test.inl
/// @brief FileSystem symlink-policy test.

/// @brief Verifies final and intermediate symlink handling for every query policy.
void testSymlinkPolicies(TestSupport::Context &context, const std::filesystem::path &root)
{
    const std::filesystem::path targetDirectory = root / "symlinks" / "target";
    const std::filesystem::path targetFile = targetDirectory / "file.txt";
    const std::filesystem::path finalFileLink = root / "symlinks" / "final-file-link";
    const std::filesystem::path intermediateDirectoryLink = root / "symlinks" / "intermediate-directory-link";

    const auto directorySetup = TestSupport::createDirectories(targetDirectory);

    if(!context.expectTrue("symlink fixture directory creation succeeds", directorySetup.ok()))
    {
        context.info(TestSupport::formatInfrastructureStatus(directorySetup));
        return;
    }

    const auto fileSetup = TestSupport::writeTextFile(targetFile, "payload");

    if(!context.expectTrue("symlink fixture file write succeeds", fileSetup.ok()))
    {
        context.info(TestSupport::formatInfrastructureStatus(fileSetup));
        return;
    }

    const auto doNotFollow = queryOptions(FileSystem::Types::SymlinkPolicy::DoNotFollow);
    const auto followFinal = queryOptions(FileSystem::Types::SymlinkPolicy::FollowFinal);
    const auto followAll = queryOptions(FileSystem::Types::SymlinkPolicy::FollowAll);

    // Check final-link queries, opens, and mutations.
    if(createFileSymlink(context, targetFile, finalFileLink))
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

    // Check traversal through an intermediate directory link.
    if(createDirectorySymlink(context, targetDirectory, intermediateDirectoryLink))
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
