/// @file filesystem_api_contract_test.cpp
/// @brief Compile-time and runtime checks for the FileSystem public API.

#include "test/filesystem_test.h"

#include "filesystem/filesystem.h"
#include "test_support/test_support.h"

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <format>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <type_traits>
#include <utility>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace
{
    namespace FileSystem = GameWIP::FileSystem;
    namespace IO = GameWIP::IO;

    using Path = FileSystem::Types::Path;
    using ConstByteSpan = std::span<const std::byte>;

    static_assert(
        std::is_same_v<decltype(FileSystem::writeAllBytes(std::declval<const Path &>(), std::declval<ConstByteSpan>())), IO::Types::WriteResult>);
    static_assert(
        std::is_same_v<decltype(FileSystem::writeAllText(std::declval<const Path &>(), std::declval<std::string_view>())), IO::Types::WriteResult>);
    static_assert(
        std::is_same_v<decltype(FileSystem::appendBytes(std::declval<const Path &>(), std::declval<ConstByteSpan>())), IO::Types::WriteResult>);
    static_assert(
        std::is_same_v<decltype(FileSystem::appendText(std::declval<const Path &>(), std::declval<std::string_view>())), IO::Types::WriteResult>);
    static_assert(
        std::is_same_v<decltype(FileSystem::writeAllBytesAtomic(std::declval<const Path &>(), std::declval<ConstByteSpan>())), IO::Types::Status>);

    static_assert(noexcept(FileSystem::writeAllBytes(std::declval<const Path &>(), std::declval<ConstByteSpan>())));
    static_assert(noexcept(FileSystem::appendBytes(std::declval<const Path &>(), std::declval<ConstByteSpan>())));
    static_assert(noexcept(FileSystem::resizeFile(std::declval<const Path &>(), 0)));
    static_assert(noexcept(FileSystem::truncateFile(std::declval<const Path &>())));

    constexpr FileSystem::Types::FileShare kReadDelete = FileSystem::Types::FileShare::Read | FileSystem::Types::FileShare::Delete;
    static_assert((kReadDelete & FileSystem::Types::FileShare::Read) == FileSystem::Types::FileShare::Read);
    static_assert((kReadDelete & FileSystem::Types::FileShare::Write) == FileSystem::Types::FileShare::None);

    static_assert(FileSystem::Types::FileOpenOptions{}.share == FileSystem::Types::FileShare::All);
    static_assert(FileSystem::Types::FileReaderOpenOptions{}.share == FileSystem::Types::FileShare::All);
    static_assert(FileSystem::Types::FileWriterOpenOptions{}.share == FileSystem::Types::FileShare::All);
    static_assert(FileSystem::Types::WriteFileOptions{}.share == FileSystem::Types::FileShare::All);
    static_assert(FileSystem::Types::AppendFileOptions{}.share == FileSystem::Types::FileShare::All);

    static_assert(FileSystem::Types::FileOpenOptions{}.symlinkPolicy == FileSystem::Types::SymlinkPolicy::FollowAll);
    static_assert(FileSystem::Types::AtomicWriteOptions{}.symlinkPolicy == FileSystem::Types::SymlinkPolicy::FollowAll);
    static_assert(FileSystem::Types::MutationOptions{}.symlinkPolicy == FileSystem::Types::SymlinkPolicy::FollowAll);
    static_assert(FileSystem::Types::QueryOptions{}.symlinkPolicy == FileSystem::Types::SymlinkPolicy::DoNotFollow);
    static_assert(FileSystem::Types::RemoveOptions{}.symlinkPolicy == FileSystem::Types::SymlinkPolicy::DoNotFollow);

    static_assert(std::is_move_constructible_v<FileSystem::File>);
    static_assert(!std::is_move_assignable_v<FileSystem::File>);
    static_assert(std::is_move_constructible_v<FileSystem::FileReader>);
    static_assert(!std::is_move_assignable_v<FileSystem::FileReader>);
    static_assert(std::is_move_constructible_v<FileSystem::FileWriter>);
    static_assert(!std::is_move_assignable_v<FileSystem::FileWriter>);
    static_assert(std::is_move_constructible_v<FileSystem::FileLock>);
    static_assert(!std::is_move_assignable_v<FileSystem::FileLock>);
    static_assert(std::is_same_v<decltype(FileSystem::Types::AtomicWriteOptions{}.temporaryNamePrefix), std::string>);

    namespace TestSupport = GameWIP::TestSupport;
    using ErrorCode = IO::Types::ErrorCode;

    std::filesystem::path makeRunRoot()
    {
        const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
        const auto threadHash = std::hash<std::thread::id>{}(std::this_thread::get_id());
        return std::filesystem::temp_directory_path() / std::format("filesystem_tests_{}_{}", ticks, threadHash);
    }

    FileSystem::Types::QueryOptions queryOptions(FileSystem::Types::SymlinkPolicy policy) noexcept
    {
        return FileSystem::Types::QueryOptions{.symlinkPolicy = policy};
    }

    void testBasicEntryQueries(TestSupport::Context &context, const std::filesystem::path &root)
    {
        const std::filesystem::path directory = root / "basic" / "directory";
        const std::filesystem::path file = root / "basic" / "file.txt";
        const std::filesystem::path missing = root / "basic" / "missing.txt";

        TestSupport::createDirectories(directory);
        TestSupport::writeTextFile(file, "payload");

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

    bool createSymlink(TestSupport::Context &context, const std::filesystem::path &target, const std::filesystem::path &link, bool directory)
    {
#if defined(_WIN32)
        const DWORD flags =
            (directory ? SYMBOLIC_LINK_FLAG_DIRECTORY : 0U) | SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE;
        if (CreateSymbolicLinkW(link.wstring().c_str(), target.wstring().c_str(), flags) == FALSE)
        {
            const DWORD error = GetLastError();
            context.skip(
                directory ? "intermediate symlink policy checks" : "file symlink policy checks",
                std::format("symlink creation unavailable: {}", std::system_category().message(static_cast<int>(error))));
            return false;
        }
        return true;
#else
        std::error_code error;
        if (directory)
        {
            std::filesystem::create_directory_symlink(target, link, error);
        }
        else
        {
            std::filesystem::create_symlink(target, link, error);
        }
        if (error)
        {
            context.skip(
                directory ? "intermediate symlink policy checks" : "file symlink policy checks",
                std::format("symlink creation unavailable: {}", error.message()));
            return false;
        }
        return true;
#endif
    }

    bool createFileSymlink(TestSupport::Context &context, const std::filesystem::path &target, const std::filesystem::path &link)
    {
        return createSymlink(context, target, link, false);
    }

    bool createDirectorySymlink(TestSupport::Context &context, const std::filesystem::path &target, const std::filesystem::path &link)
    {
        return createSymlink(context, target, link, true);
    }

    void testSymlinkPolicies(TestSupport::Context &context, const std::filesystem::path &root)
    {
        const std::filesystem::path targetDirectory = root / "symlinks" / "target";
        const std::filesystem::path targetFile = targetDirectory / "file.txt";
        const std::filesystem::path finalFileLink = root / "symlinks" / "final-file-link";
        const std::filesystem::path intermediateDirectoryLink = root / "symlinks" / "intermediate-directory-link";

        TestSupport::createDirectories(targetDirectory);
        TestSupport::writeTextFile(targetFile, "payload");

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
        }

        if (createDirectorySymlink(context, targetDirectory, intermediateDirectoryLink))
        {
            const std::filesystem::path pathThroughIntermediate = intermediateDirectoryLink / "file.txt";

            const auto followAllThroughIntermediate = FileSystem::isRegularFile(pathThroughIntermediate, followAll);
            static_cast<void>(
                context.expectTrue("FollowAll allows intermediate symlink", followAllThroughIntermediate.status.ok()));
            static_cast<void>(
                context.expectTrue("FollowAll reaches file through intermediate symlink", followAllThroughIntermediate.value));

            const auto noFollowThroughIntermediate = FileSystem::isRegularFile(pathThroughIntermediate, doNotFollow);
            static_cast<void>(context.expectEq(
                "DoNotFollow rejects intermediate symlink",
                ErrorCode::PermissionDenied,
                noFollowThroughIntermediate.status.code));

            const auto followFinalThroughIntermediate = FileSystem::isRegularFile(pathThroughIntermediate, followFinal);
            static_cast<void>(context.expectEq(
                "FollowFinal rejects intermediate symlink",
                ErrorCode::PermissionDenied,
                followFinalThroughIntermediate.status.code));
        }
    }
} // namespace

namespace GameWIP::Test
{
    int runFileSystemTests(int, char **, const FileSystemTestOptions &options)
    {
        const std::filesystem::path runRoot = makeRunRoot();
        TestSupport::createDirectories(runRoot);

        TestSupport::Types::ReportOptions reportOptions;
        reportOptions.writeConsole = true;
        reportOptions.writeReport = options.writeReport;
        reportOptions.appendReport = options.appendReport;
        reportOptions.reportPath = options.reportPath;

        TestSupport::Runner runner(reportOptions);
        runner.info(std::format("FileSystem library test root: {}", runRoot.string()));

        runner.runSuite(
            "FileSystem entry queries",
            [&runRoot](TestSupport::Context &context)
            {
                testBasicEntryQueries(context, runRoot);
            });
        runner.runSuite(
            "FileSystem symlink policies",
            [&runRoot](TestSupport::Context &context)
            {
                testSymlinkPolicies(context, runRoot);
            });

        const TestSupport::Types::Summary result = runner.result();
        runner.summary(std::format("FileSystem library self-tests passed={} failed={} skipped={}", result.passed, result.failed, result.skipped));

        TestSupport::removeIfExists(runRoot);
        return runner.exitCode();
    }
} // namespace GameWIP::Test
