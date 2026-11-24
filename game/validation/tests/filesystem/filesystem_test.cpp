/// @file filesystem_test.cpp
/// @brief Compile-time and runtime checks for the FileSystem public API.
///
/// Tests use an isolated temporary workspace and group coverage by metadata,
/// symlink policy, whole-file I/O, mutation, atomic replacement, and locking.

#include "validation/tests/filesystem/filesystem_test.h"
#include "validation/tests/filesystem/platform.h"

#include "filesystem/filesystem.h"
#include "filesystem/internal/platform.h"
#include "validation/tests/internal/helpers/runner_helpers.h"
#include "test_support/test_support.h"

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <format>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>

namespace
{
    namespace FileSystem = GameWIP::FileSystem;
    namespace IO = GameWIP::IO;
    namespace FileSystemPlatform = GameWIP::Test::FileSystemPlatform;

    using FileSystem::Types::Path;
    using ConstByteSpan = std::span<const std::byte>;

    // Whole-file result types
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

    // Non-throwing operation contracts
    static_assert(noexcept(FileSystem::writeAllBytes(std::declval<const Path &>(), std::declval<ConstByteSpan>())));
    static_assert(noexcept(FileSystem::appendBytes(std::declval<const Path &>(), std::declval<ConstByteSpan>())));
    static_assert(noexcept(FileSystem::resizeFile(std::declval<const Path &>(), 0)));
    static_assert(noexcept(FileSystem::truncateFile(std::declval<const Path &>())));

    // Default options and share flags
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

    // Handle ownership contracts
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

    // Public result and option types
    static_assert(std::is_same_v<decltype(FileSystem::Types::File::AtomicWriteOptions{}.temporaryNamePrefix), std::string>);
    static_assert(std::is_same_v<decltype(FileSystem::Types::Directory::ListResult{}.entries), std::vector<FileSystem::Types::Directory::Entry>>);
    static_assert(std::is_same_v<decltype(FileSystem::Types::Lock::Result{}.outcome), FileSystem::Types::Lock::Outcome>);

    namespace TestSupport = GameWIP::TestSupport;
    using IO::Types::ErrorCode;

    /// ------------------------------------------------------------
    /// Test helpers
    /// ------------------------------------------------------------

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

    /// @brief Checks native query success before comparing its payload.
    bool expectStatus(TestSupport::Context &context, std::string_view name, const IO::Types::Status &status)
    {
        if(!status.ok())
        {
            context.fail(name, std::format("code={} native={} message={}", IO::errorCodeName(status.code), status.nativeCode, status.message));
            return false;
        }

        context.pass(name);
        return true;
    }

    /// @brief Checks a whole-file query before inspecting retained content.
    void expectFileText(TestSupport::Context &context, std::string_view name, const Path &path, std::string_view expected)
    {
        const auto result = FileSystem::readAllText(path);

        if(expectStatus(context, std::string(name) + " query", result.status))
        {
            static_cast<void>(context.expectEq(name, expected, result.text));
        }
    }

    /// @brief Checks a predicate query before inspecting its boolean value.
    void expectPredicate(TestSupport::Context &context, std::string_view name, const FileSystem::Types::BoolResult &result, bool expected)
    {
        if(expectStatus(context, std::string(name) + " query", result.status))
        {
            static_cast<void>(context.expectEq(name, expected, result.value));
        }
    }

    /// @brief Creates a file or directory symlink and records unavailable host support as a skip.
    [[nodiscard]] bool createSymlink(
        TestSupport::Context &context,
        const std::filesystem::path &target,
        const std::filesystem::path &link,
        bool directory)
    {
        return FileSystemPlatform::createSymlink(context, target, link, directory);
    }

    /// @brief Creates a file symlink.
    [[nodiscard]] bool createFileSymlink(TestSupport::Context &context, const std::filesystem::path &target, const std::filesystem::path &link)
    {
        return createSymlink(context, target, link, false);
    }

    /// @brief Creates a directory symlink through the shared privilege-aware helper.
    [[nodiscard]] bool createDirectorySymlink(TestSupport::Context &context, const std::filesystem::path &target, const std::filesystem::path &link)
    {
        return createSymlink(context, target, link, true);
    }

    /// ------------------------------------------------------------
    /// Test suites
    /// ------------------------------------------------------------

    // Focused suite declarations keep cross-suite calls independent of fragment include order.
    void testBasicEntryQueries(TestSupport::Context &context, const std::filesystem::path &root);
    void testSymlinkPolicies(TestSupport::Context &context, const std::filesystem::path &root);
    void testMetadataDirectoriesAndListing(TestSupport::Context &context, const std::filesystem::path &root);
    void testUtf8PathConversion(TestSupport::Context &context, const std::filesystem::path &root);
    void testTextContracts(TestSupport::Context &context, const std::filesystem::path &root);
    void testWholeFileHelpersAndHandles(TestSupport::Context &context, const std::filesystem::path &root);
    void testMutationCopyMoveAndRemoval(TestSupport::Context &context, const std::filesystem::path &root);
    void testAtomicWriteAndLocks(TestSupport::Context &context, const std::filesystem::path &root);

#include "validation/tests/filesystem/symlink_test.inl"
#include "validation/tests/filesystem/text_test.inl"
#include "validation/tests/filesystem/handles_test.inl"
#include "validation/tests/filesystem/mutation_test.inl"
#include "validation/tests/filesystem/atomic_lock_test.inl"
#include "validation/tests/filesystem/metadata_test.inl"
#include "validation/tests/filesystem/path_test.inl"
} // namespace

namespace GameWIP::Test
{
    /// ------------------------------------------------------------
    /// Test runner
    /// ------------------------------------------------------------

    int runFileSystemTests(int, char **, const FileSystemTestOptions &options)
    {
        const TestSupport::ScopedTemporaryDirectory workspace("filesystem_tests");

        if(!workspace.status().ok())
        {
            std::cerr << "FileSystem could not create its test workspace: " << TestSupport::formatInfrastructureStatus(workspace.status()) << '\n';
            return 1;
        }
        const std::filesystem::path &runRoot = workspace.path();

        const TestSupport::Types::Reporting::Options reportOptions = GameWIP::Test::Internal::RunnerHelpers::makeReportingOptions(options);

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
        FileSystemPlatform::registerSuites(runner, runRoot);

        const TestSupport::Types::Reporting::Summary result = runner.result();
        runner.summary(std::format("FileSystem library self-tests passed={} failed={} skipped={}", result.passed, result.failed, result.skipped));

        return GameWIP::Test::Internal::RunnerHelpers::finalizeRunner(runner);
    }
} // namespace GameWIP::Test
