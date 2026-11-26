/// @file atomic_lock_test.inl
/// @brief FileSystem atomic-write and lock test.

/// @brief Verifies atomic replacement and non-blocking whole-file lock ownership.
void testAtomicWriteAndLocks(TestSupport::Context &context, const std::filesystem::path &root)
{
    const std::filesystem::path file = root / "atomic-locks" / "file.txt";

    // Verify replacement content and conflict handling.
    static_cast<void>(context.expectTrue("initial atomic test write succeeds", FileSystem::writeAllText(file, "old").ok()));
    const auto atomicReplace = FileSystem::writeAllTextAtomic(file, "new");
    static_cast<void>(context.expectTrue("writeAllTextAtomic replace succeeds", atomicReplace.ok()));

    const auto replaced = FileSystem::readAllText(file);
    static_cast<void>(context.expectTrue("read atomic replacement succeeds", replaced.ok()));
    if(replaced.ok())
    {
        static_cast<void>(context.expectEq("atomic replacement content", std::string{"new"}, replaced.value()));
    }

    const auto atomicFailIfExists = FileSystem::writeAllTextAtomic(
        file,
        "bad",
        FileSystem::Types::File::AtomicWriteOptions{.replaceMode = FileSystem::Types::ReplaceMode::FailIfExists});
    static_cast<void>(context.expectEq("atomic fail-if-exists reports AlreadyExists", ErrorCode::AlreadyExists, atomicFailIfExists.status().code()));
    expectFileText(context, "atomic fail-if-exists preserves content", file, "new");

    // Verify exclusive lock outcomes and close restrictions.
    FileSystem::File lockFile;
    const auto openStatus = lockFile.open(
        file,
        FileSystem::Types::File::OpenOptions{
            .access = FileSystem::Types::File::Access::ReadWrite,
            .mode = FileSystem::Types::File::OpenMode::OpenExisting});
    static_cast<void>(context.expectTrue("lock file opens", openStatus.ok()));

    auto lock = lockFile.tryLockExclusive();
    static_cast<void>(context.expectTrue("exclusive lock call succeeds", lock.ok()));
    if(!lock.ok())
    {
        return;
    }
    FileSystem::FileLock acquiredLock = std::move(lock).value();

    FileSystem::File competingLockFile;
    const auto competingOpenStatus = competingLockFile.open(
        file,
        FileSystem::Types::File::OpenOptions{
            .access = FileSystem::Types::File::Access::ReadWrite,
            .mode = FileSystem::Types::File::OpenMode::OpenExisting});
    static_cast<void>(context.expectTrue("competing lock file opens", competingOpenStatus.ok()));
    const auto competingLock = competingLockFile.tryLockExclusive();
    static_cast<void>(context.expectFalse("competing exclusive lock would block", competingLock.ok()));
    static_cast<void>(context.expectEq("competing exclusive lock reports WouldBlock", ErrorCode::WouldBlock, competingLock.status().code()));
    static_cast<void>(context.expectTrue("competing lock file closes", competingLockFile.close().ok()));

    static_cast<void>(context.expectEq("close while locked reports ResourceBusy", ErrorCode::ResourceBusy, lockFile.close().status().code()));
    static_cast<void>(context.expectTrue("unlock succeeds", acquiredLock.unlock().ok()));
    static_cast<void>(context.expectTrue("close after unlock succeeds", lockFile.close().ok()));

    // Verify lock ownership survives destruction of the source handle.
    const std::filesystem::path detachedOwnerFile = root / "atomic-locks" / "detached-owner.txt";
    static_cast<void>(context.expectTrue("detached-owner file write succeeds", FileSystem::writeAllText(detachedOwnerFile, "lock").ok()));
    FileSystem::FileLock detachedOwnerLock = [&]
    {
        FileSystem::File owner;
        static_cast<void>(context.expectTrue("detached lock owner opens", owner.open(detachedOwnerFile).ok()));
        auto result = owner.tryLockExclusive();
        static_cast<void>(context.expectTrue("detached owner acquires lock", result.ok()));
        if(!result.ok())
        {
            return FileSystem::FileLock{};
        }
        return std::move(result).value();
    }();

    FileSystem::File detachedCompetitor;
    static_cast<void>(context.expectTrue("detached lock competitor opens", detachedCompetitor.open(detachedOwnerFile).ok()));
    const auto blockedByDetachedOwner = detachedCompetitor.tryLockExclusive();
    static_cast<void>(context.expectEq(
        "lock remains active after source handle destruction",
        ErrorCode::WouldBlock,
        blockedByDetachedOwner.status().code()));
    static_cast<void>(context.expectTrue("detached owner unlock succeeds", detachedOwnerLock.unlock().ok()));
    auto acquiredAfterDetachedUnlock = detachedCompetitor.tryLockExclusive();
    static_cast<void>(context.expectTrue("competitor acquires after detached owner unlock", acquiredAfterDetachedUnlock.ok()));
    if(acquiredAfterDetachedUnlock.ok())
    {
        static_cast<void>(context.expectTrue("detached competitor unlock succeeds", std::move(acquiredAfterDetachedUnlock).value().unlock().ok()));
    }
    static_cast<void>(context.expectTrue("detached competitor closes", detachedCompetitor.close().ok()));
}
