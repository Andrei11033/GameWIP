/// @file atomic_lock_test.inl
/// @brief FileSystem atomic-write and lock test.

/// @brief Verifies atomic replacement and non-blocking whole-file lock ownership.
void testAtomicWriteAndLocks(TestSupport::Context &context, const std::filesystem::path &root)
{
    const std::filesystem::path file = root / "atomic-locks" / "file.txt";

    // Verify replacement content and conflict handling.
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
    expectFileText(context, "atomic fail-if-exists preserves content", file, "new");

    // Verify exclusive lock outcomes and close restrictions.
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
    const auto competingLock = competingLockFile.tryLockExclusive();
    static_cast<void>(context.expectTrue("competing exclusive lock call succeeds", competingLock.status.ok()));
    static_cast<void>(context.expectEq("competing exclusive lock would block", FileSystem::Types::Lock::Outcome::WouldBlock, competingLock.outcome));
    static_cast<void>(context.expectTrue("competing lock file closes", competingLockFile.close().ok()));

    static_cast<void>(context.expectEq("close while locked reports ResourceBusy", ErrorCode::ResourceBusy, lockFile.close().code));
    static_cast<void>(context.expectTrue("unlock succeeds", lock.lock.unlock().ok()));
    static_cast<void>(context.expectTrue("close after unlock succeeds", lockFile.close().ok()));

    // Verify lock ownership survives destruction of the source handle.
    const std::filesystem::path detachedOwnerFile = root / "atomic-locks" / "detached-owner.txt";
    static_cast<void>(context.expectTrue("detached-owner file write succeeds", FileSystem::writeAllText(detachedOwnerFile, "lock").status.ok()));
    FileSystem::FileLock detachedOwnerLock = [&]
    {
        FileSystem::File owner;
        static_cast<void>(context.expectTrue("detached lock owner opens", owner.open(detachedOwnerFile).ok()));
        auto result = owner.tryLockExclusive();
        static_cast<void>(context.expectEq("detached owner acquires lock", FileSystem::Types::Lock::Outcome::Acquired, result.outcome));
        return std::move(result.lock);
    }();

    FileSystem::File detachedCompetitor;
    static_cast<void>(context.expectTrue("detached lock competitor opens", detachedCompetitor.open(detachedOwnerFile).ok()));
    const auto blockedByDetachedOwner = detachedCompetitor.tryLockExclusive();
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
}
