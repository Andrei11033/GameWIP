/// @file filesystem_test.cpp
/// @brief Compile-time and runtime checks for the FileSystem public API.
///
/// Tests use an isolated temporary workspace and group coverage by metadata,
/// symlink policy, whole-file I/O, mutation, atomic replacement, and locking.

#include "validation/tests/filesystem/filesystem_test.h"

#include "filesystem/filesystem.h"
#include "filesystem/internal/filesystem_platform.h"
#include "test_support/test_support.h"

#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <functional>
#include <iostream>
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

    constexpr FileSystem::Types::File::Share kReadDelete = FileSystem::Types::File::Share::Read | FileSystem::Types::File::Share::Delete;
    static_assert((kReadDelete & FileSystem::Types::File::Share::Read) == FileSystem::Types::File::Share::Read);
    static_assert((kReadDelete & FileSystem::Types::File::Share::Write) == FileSystem::Types::File::Share::None);

    static_assert(FileSystem::Types::File::OpenOptions{}.share == FileSystem::Types::File::Share::All);
    static_assert(FileSystem::Types::File::ReaderOpenOptions{}.share == FileSystem::Types::File::Share::All);
    static_assert(FileSystem::Types::File::WriterOpenOptions{}.share == FileSystem::Types::File::Share::All);
    static_assert(FileSystem::Types::File::WriteOptions{}.share == FileSystem::Types::File::Share::All);
    static_assert(FileSystem::Types::File::AppendOptions{}.share == FileSystem::Types::File::Share::All);

    static_assert(FileSystem::Types::File::OpenOptions{}.symlinkPolicy == FileSystem::Types::SymlinkPolicy::DoNotFollow);
    static_assert(FileSystem::Types::File::AtomicWriteOptions{}.symlinkPolicy == FileSystem::Types::SymlinkPolicy::DoNotFollow);
    static_assert(FileSystem::Types::File::AtomicWriteOptions{}.flushParentDirectory);
    static_assert(FileSystem::Types::File::ResizeOptions{}.symlinkPolicy == FileSystem::Types::SymlinkPolicy::DoNotFollow);
    static_assert(FileSystem::Types::EntryOptions{}.symlinkPolicy == FileSystem::Types::SymlinkPolicy::DoNotFollow);
    static_assert(FileSystem::Types::RemoveOptions{}.symlinkPolicy == FileSystem::Types::SymlinkPolicy::DoNotFollow);

    static_assert(std::is_move_constructible_v<FileSystem::File>);
    static_assert(!std::is_move_assignable_v<FileSystem::File>);
    static_assert(std::is_move_constructible_v<FileSystem::FileReader>);
    static_assert(!std::is_move_assignable_v<FileSystem::FileReader>);
    static_assert(std::is_move_constructible_v<FileSystem::FileWriter>);
    static_assert(!std::is_move_assignable_v<FileSystem::FileWriter>);
    static_assert(std::is_move_constructible_v<FileSystem::FileLock>);
    static_assert(!std::is_move_assignable_v<FileSystem::FileLock>);
    static_assert(std::is_move_constructible_v<FileSystem::DirectoryCursor>);
    static_assert(std::is_move_assignable_v<FileSystem::DirectoryCursor>);
    static_assert(!std::is_copy_constructible_v<FileSystem::DirectoryCursor>);
    static_assert(!std::is_copy_assignable_v<FileSystem::DirectoryCursor>);
    static_assert(std::is_same_v<decltype(FileSystem::Types::File::AtomicWriteOptions{}.temporaryNamePrefix), std::string>);
    static_assert(std::is_same_v<decltype(FileSystem::Types::Directory::ListResult{}.entries), std::vector<FileSystem::Types::Directory::Entry>>);
    static_assert(std::is_same_v<decltype(FileSystem::Types::Lock::Result{}.outcome), FileSystem::Types::Lock::Outcome>);

    namespace TestSupport = GameWIP::TestSupport;
    using ErrorCode = IO::Types::ErrorCode;

    /// @brief Creates entry-query options for one explicit symlink policy.
    FileSystem::Types::EntryOptions queryOptions(FileSystem::Types::SymlinkPolicy policy) noexcept
    {
        return FileSystem::Types::EntryOptions{.symlinkPolicy = policy};
    }

    /// @brief Exposes fixture text as bytes without copying or conversion.
    std::span<const std::byte> bytesOf(std::string_view text) noexcept
    {
        return std::as_bytes(std::span<const char>(text.data(), text.size()));
    }

    /// @brief Creates a file or directory symlink and records unavailable host support as a skip.
    bool createSymlink(TestSupport::Context &context, const std::filesystem::path &target, const std::filesystem::path &link, bool directory)
    {
#if defined(_WIN32)
        const DWORD flags = (directory ? SYMBOLIC_LINK_FLAG_DIRECTORY : 0U) | SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE;
        if (CreateSymbolicLinkW(link.wstring().c_str(), target.wstring().c_str(), flags) == FALSE)
        {
            const DWORD error = GetLastError();
            if (std::getenv("GAMEWIP_REQUIRE_SYMLINK_TESTS") != nullptr)
            {
                context.fail(
                    directory ? "intermediate symlink policy checks" : "file symlink policy checks",
                    std::format("CI requires symlink creation: {}", std::system_category().message(static_cast<int>(error))));
                return false;
            }
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
            if (std::getenv("GAMEWIP_REQUIRE_SYMLINK_TESTS") != nullptr)
            {
                context.fail(
                    directory ? "intermediate symlink policy checks" : "file symlink policy checks",
                    std::format("CI requires symlink creation: {}", error.message()));
                return false;
            }
            context.skip(
                directory ? "intermediate symlink policy checks" : "file symlink policy checks",
                std::format("symlink creation unavailable: {}", error.message()));
            return false;
        }
        return true;
#endif
    }

    /// @brief Creates a file symlink through the shared privilege-aware helper.
    bool createFileSymlink(TestSupport::Context &context, const std::filesystem::path &target, const std::filesystem::path &link)
    {
        return createSymlink(context, target, link, false);
    }

    /// @brief Creates a directory symlink through the shared privilege-aware helper.
    bool createDirectorySymlink(TestSupport::Context &context, const std::filesystem::path &target, const std::filesystem::path &link)
    {
        return createSymlink(context, target, link, true);
    }

    // Focused suite declarations keep cross-suite calls independent of fragment include order.
    void testBasicEntryQueries(TestSupport::Context &context, const std::filesystem::path &root);
    void testSymlinkPolicies(TestSupport::Context &context, const std::filesystem::path &root);
    void testMetadataDirectoriesAndListing(TestSupport::Context &context, const std::filesystem::path &root);
    void testUtf8PathConversion(TestSupport::Context &context, const std::filesystem::path &root);
    void testTextContracts(TestSupport::Context &context, const std::filesystem::path &root);
    void testWholeFileHelpersAndHandles(TestSupport::Context &context, const std::filesystem::path &root);
    void testMutationCopyMoveAndRemoval(TestSupport::Context &context, const std::filesystem::path &root);
    void testAtomicWriteAndLocks(TestSupport::Context &context, const std::filesystem::path &root);
#if FILESYSTEM_INTERNAL_TEST_HOOKS
    void testCheckedFileFailureTranslation(TestSupport::Context &context, const std::filesystem::path &root);
#endif

#include "validation/tests/filesystem/file_test.inl"
#include "validation/tests/filesystem/metadata_test.inl"
#include "validation/tests/filesystem/path_test.inl"
} // namespace

namespace GameWIP::Test
{
    int runFileSystemTests(int, char **, const FileSystemTestOptions &options)
    {
        const TestSupport::ScopedTemporaryDirectory workspace("filesystem_tests");
        if (!workspace.status().ok())
        {
            std::cerr << "FileSystem could not create its test workspace: " << TestSupport::formatInfrastructureStatus(workspace.status()) << '\n';
            return 1;
        }
        const std::filesystem::path &runRoot = workspace.path();

        TestSupport::Types::Reporting::Options reportOptions;
        reportOptions.writeConsole = true;
        reportOptions.consoleVerbosity =
            options.verboseConsole ? TestSupport::Types::Reporting::ConsoleVerbosity::Full : TestSupport::Types::Reporting::ConsoleVerbosity::Minimal;
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
        runner.runSuite(
            "FileSystem metadata directories listing",
            [&runRoot](TestSupport::Context &context)
            {
                testMetadataDirectoriesAndListing(context, runRoot);
            });
        runner.runSuite(
            "FileSystem whole-file helpers and handles",
            [&runRoot](TestSupport::Context &context)
            {
                testWholeFileHelpersAndHandles(context, runRoot);
            });
        runner.runSuite(
            "FileSystem UTF-8 paths",
            [&runRoot](TestSupport::Context &context)
            {
                testUtf8PathConversion(context, runRoot);
            });
        runner.runSuite(
            "FileSystem UTF-8 text contracts",
            [&runRoot](TestSupport::Context &context)
            {
                testTextContracts(context, runRoot);
            });
        runner.runSuite(
            "FileSystem mutation copy move removal",
            [&runRoot](TestSupport::Context &context)
            {
                testMutationCopyMoveAndRemoval(context, runRoot);
            });
        runner.runSuite(
            "FileSystem atomic write and locks",
            [&runRoot](TestSupport::Context &context)
            {
                testAtomicWriteAndLocks(context, runRoot);
            });
#if FILESYSTEM_INTERNAL_TEST_HOOKS
        runner.runSuite(
            "FileSystem checked failure translation",
            [&runRoot](TestSupport::Context &context)
            {
                testCheckedFileFailureTranslation(context, runRoot);
            });
#endif

        const TestSupport::Types::Reporting::Summary result = runner.result();
        runner.summary(std::format("FileSystem library self-tests passed={} failed={} skipped={}", result.passed, result.failed, result.skipped));

        return runner.exitCode();
    }
} // namespace GameWIP::Test
