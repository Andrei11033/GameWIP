/// @file filesystem.cpp
/// @brief Win32 FileSystem validation fixtures.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "validation/tests/filesystem/platform.h"

#include "filesystem/filesystem.h"
#include "filesystem/internal/filesystem_platform.h"
#include "test_support/test_support.h"

#include <windows.h>

#include <chrono>
#include <cstdlib>
#include <format>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>

namespace
{
    namespace FileSystem = GameWIP::FileSystem;
    namespace TestSupport = GameWIP::TestSupport;

#if FILESYSTEM_INTERNAL_TEST_HOOKS

    /// ------------------------------------------------------------
    /// Move synchronization
    /// ------------------------------------------------------------

    void expectExists(TestSupport::Context &context, std::string_view name, const std::filesystem::path &path, bool expected)
    {
        const auto result = FileSystem::exists(path);

        if(!result.status.ok())
        {
            context.fail(name, std::format("existence query failed: native={}", result.status.nativeCode));
            return;
        }

        static_cast<void>(context.expectEq(name, expected, result.value));
    }

    void testMoveSynchronization(TestSupport::Context &context, const std::filesystem::path &root)
    {
        // Replace the destination parent while strict move validation is paused.
        const std::filesystem::path directory = root / "win32-move-synchronization";
        const std::filesystem::path raceSource = directory / "move-race-source.txt";
        const std::filesystem::path validatedParent = directory / "validated-parent";
        const std::filesystem::path parkedParent = directory / "validated-parent-parked";
        const std::filesystem::path destination = validatedParent / "destination.txt";

        static_cast<void>(context.expectTrue("create move synchronization directory", FileSystem::createDirectories(directory).ok()));
        static_cast<void>(context.expectTrue("write move race source", FileSystem::writeAllText(raceSource, "anchored").status.ok()));
        static_cast<void>(context.expectTrue("create validated move parent", FileSystem::createDirectory(validatedParent).ok()));

        FileSystem::Detail::Platform::TestHooks::armMoveDestinationValidatedPause();
        GameWIP::IO::Types::Status moveRaceStatus;
        std::thread moveThread(
            [&]
            {
                moveRaceStatus = FileSystem::movePath(raceSource, destination);
            });
        const bool movePaused = FileSystem::Detail::Platform::TestHooks::waitForMovePause(std::chrono::seconds{5});
        static_cast<void>(context.expectTrue("strict move reaches destination validation pause", movePaused));

        if(movePaused)
        {
            std::error_code renameError;
            std::filesystem::rename(validatedParent, parkedParent, renameError);
            static_cast<void>(context.expectFalse("validated parent can be renamed during pause", static_cast<bool>(renameError)));
            static_cast<void>(context.expectTrue("create replacement move parent", FileSystem::createDirectory(validatedParent).ok()));
        }

        FileSystem::Detail::Platform::TestHooks::releaseMovePause();
        moveThread.join();

        static_cast<void>(context.expectTrue("anchored strict move succeeds", moveRaceStatus.ok()));
        expectExists(context, "anchored move lands in retained parent", parkedParent / "destination.txt", true);
        expectExists(context, "anchored move avoids replacement parent", destination, false);
        FileSystem::Detail::Platform::TestHooks::reset();

        // Mutate the source and destination after the native move commits.
        const std::filesystem::path commitSource = directory / "commit-source.txt";
        const std::filesystem::path commitDestination = directory / "commit-destination.txt";
        static_cast<void>(context.expectTrue("write post-commit source", FileSystem::writeAllText(commitSource, "original").status.ok()));

        FileSystem::Detail::Platform::TestHooks::armMoveCommittedPause();
        GameWIP::IO::Types::Status committedStatus;
        std::thread commitThread(
            [&]
            {
                committedStatus = FileSystem::movePath(commitSource, commitDestination);
            });
        const bool commitPaused = FileSystem::Detail::Platform::TestHooks::waitForMovePause(std::chrono::seconds{5});
        static_cast<void>(context.expectTrue("move reaches committed pause", commitPaused));

        if(commitPaused)
        {
            static_cast<void>(
                context.expectTrue("source can be recreated after commit", FileSystem::writeAllText(commitSource, "replacement").status.ok()));
            static_cast<void>(context.expectTrue("destination can be removed after commit", FileSystem::removeFile(commitDestination).ok()));
        }

        FileSystem::Detail::Platform::TestHooks::releaseMovePause();
        commitThread.join();

        static_cast<void>(context.expectTrue("native commit remains successful after namespace mutation", committedStatus.ok()));
        const auto recreatedSource = FileSystem::readAllText(commitSource);
        static_cast<void>(context.expectTrue("recreated source query succeeds", recreatedSource.status.ok()));
        static_cast<void>(context.expectEq("recreated source remains caller-visible", std::string{"replacement"}, recreatedSource.text));
        expectExists(context, "concurrently removed destination remains absent", commitDestination, false);
        FileSystem::Detail::Platform::TestHooks::reset();
    }

    /// ------------------------------------------------------------
    /// Failed unlock recovery
    /// ------------------------------------------------------------

    void testFailedUnlockRecovery(TestSupport::Context &context, const std::filesystem::path &root)
    {
        // Hold the same file through two owners so cleanup can be tested under contention.
        const std::filesystem::path directory = root / "atomic-locks";
        const std::filesystem::path failedUnlockFile = directory / "failed-unlock.txt";

        static_cast<void>(context.expectTrue("create failed-unlock directory", FileSystem::createDirectories(directory).ok()));
        static_cast<void>(context.expectTrue("failed-unlock file write succeeds", FileSystem::writeAllText(failedUnlockFile, "lock").status.ok()));

        FileSystem::File failedUnlockOwner;
        FileSystem::File failedUnlockCompetitor;
        static_cast<void>(context.expectTrue("failed-unlock owner opens", failedUnlockOwner.open(failedUnlockFile).ok()));
        static_cast<void>(context.expectTrue("failed-unlock competitor opens", failedUnlockCompetitor.open(failedUnlockFile).ok()));

        {
            auto failedUnlockLock = failedUnlockOwner.tryLockExclusive();
            static_cast<void>(
                context.expectEq("failed-unlock owner acquires lock", FileSystem::Types::Lock::Outcome::Acquired, failedUnlockLock.outcome));

            const auto blockedBeforeCleanup = failedUnlockCompetitor.tryLockExclusive();
            static_cast<void>(context.expectEq(
                "failed-unlock competitor initially blocks",
                FileSystem::Types::Lock::Outcome::WouldBlock,
                blockedBeforeCleanup.outcome));

            FileSystem::Detail::Platform::TestHooks::setFileUnlockFailure(true);
            const FileSystem::FileLock lockDestroyedDuringFailure = std::move(failedUnlockLock.lock);
            static_cast<void>(context.expectTrue("failed-unlock injected lock remains active", lockDestroyedDuringFailure.active()));
        }

        // Closing the owner must release the native lock after injected cleanup failure.
        FileSystem::Detail::Platform::TestHooks::reset();
        static_cast<void>(context.expectTrue("failed-unlock owner count clears after lock destruction", failedUnlockOwner.close().ok()));

        auto acquiredAfterFailedUnlockCleanup = failedUnlockCompetitor.tryLockExclusive();
        static_cast<void>(context.expectEq(
            "closing owner releases native lock after failed lock cleanup",
            FileSystem::Types::Lock::Outcome::Acquired,
            acquiredAfterFailedUnlockCleanup.outcome));
        static_cast<void>(
            context.expectTrue("failed-unlock competitor releases recovered lock", acquiredAfterFailedUnlockCleanup.lock.unlock().ok()));
        static_cast<void>(context.expectTrue("failed-unlock competitor closes", failedUnlockCompetitor.close().ok()));
    }
#endif
} // namespace

namespace GameWIP::Test::FileSystemPlatform
{
    /// ------------------------------------------------------------
    /// Symlink fixtures
    /// ------------------------------------------------------------

    bool createSymlink(TestSupport::Context &context, const std::filesystem::path &target, const std::filesystem::path &link, bool directory)
    {
        const DWORD flags = (directory ? SYMBOLIC_LINK_FLAG_DIRECTORY : 0U) | SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE;

        if(CreateSymbolicLinkW(link.wstring().c_str(), target.wstring().c_str(), flags) == FALSE)
        {
            const DWORD error = GetLastError();
            const bool unavailable =
                error == ERROR_PRIVILEGE_NOT_HELD || error == ERROR_ACCESS_DENIED || error == ERROR_NOT_SUPPORTED || error == ERROR_INVALID_FUNCTION;
            const std::string message = std::system_category().message(static_cast<int>(error));

            if(std::getenv("GAMEWIP_REQUIRE_SYMLINK_TESTS") != nullptr || !unavailable)
            {
                context.fail(
                    directory ? "intermediate symlink policy checks" : "file symlink policy checks",
                    std::format("symlink creation failed: native={} message={}", error, message));
                return false;
            }

            context.skip(
                directory ? "intermediate symlink policy checks" : "file symlink policy checks",
                std::format("symlink creation unavailable: {}", message));
            return false;
        }

        return true;
    }

    /// ------------------------------------------------------------
    /// FileSystem validation suites
    /// ------------------------------------------------------------

    void registerSuites(TestSupport::Runner &runner, const std::filesystem::path &root)
    {
#if FILESYSTEM_INTERNAL_TEST_HOOKS
        runner.runSuite(
            "FileSystem move synchronization",
            [&root](TestSupport::Context &context)
            {
                testMoveSynchronization(context, root);
            });
        runner.runSuite(
            "FileSystem failed unlock recovery",
            [&root](TestSupport::Context &context)
            {
                testFailedUnlockRecovery(context, root);
            });
#endif
    }
} // namespace GameWIP::Test::FileSystemPlatform
