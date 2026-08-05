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

    constexpr FileSystem::Types::FileShare kReadDelete = FileSystem::Types::FileShare::Read | FileSystem::Types::FileShare::Delete;
    static_assert((kReadDelete & FileSystem::Types::FileShare::Read) == FileSystem::Types::FileShare::Read);
    static_assert((kReadDelete & FileSystem::Types::FileShare::Write) == FileSystem::Types::FileShare::None);

    static_assert(FileSystem::Types::FileOpenOptions{}.share == FileSystem::Types::FileShare::All);
    static_assert(FileSystem::Types::FileReaderOpenOptions{}.share == FileSystem::Types::FileShare::All);
    static_assert(FileSystem::Types::FileWriterOpenOptions{}.share == FileSystem::Types::FileShare::All);
    static_assert(FileSystem::Types::WriteFileOptions{}.share == FileSystem::Types::FileShare::All);
    static_assert(FileSystem::Types::AppendFileOptions{}.share == FileSystem::Types::FileShare::All);

    static_assert(FileSystem::Types::FileOpenOptions{}.symlinkPolicy == FileSystem::Types::SymlinkPolicy::DoNotFollow);
    static_assert(FileSystem::Types::AtomicWriteOptions{}.symlinkPolicy == FileSystem::Types::SymlinkPolicy::DoNotFollow);
    static_assert(FileSystem::Types::AtomicWriteOptions{}.flushParentDirectory);
    static_assert(FileSystem::Types::MutationOptions{}.symlinkPolicy == FileSystem::Types::SymlinkPolicy::DoNotFollow);
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
    static_assert(std::is_move_constructible_v<FileSystem::DirectoryCursor>);
    static_assert(std::is_move_assignable_v<FileSystem::DirectoryCursor>);
    static_assert(!std::is_copy_constructible_v<FileSystem::DirectoryCursor>);
    static_assert(!std::is_copy_assignable_v<FileSystem::DirectoryCursor>);
    static_assert(std::is_same_v<decltype(FileSystem::Types::AtomicWriteOptions{}.temporaryNamePrefix), std::string>);

    namespace TestSupport = GameWIP::TestSupport;
    using ErrorCode = IO::Types::ErrorCode;

    /// @brief Creates entry-query options for one explicit symlink policy.
    FileSystem::Types::QueryOptions queryOptions(FileSystem::Types::SymlinkPolicy policy) noexcept
    {
        return FileSystem::Types::QueryOptions{.symlinkPolicy = policy};
    }

    /// @brief Exposes fixture text as bytes without copying or conversion.
    std::span<const std::byte> bytesOf(std::string_view text) noexcept
    {
        return std::as_bytes(std::span<const char>(text.data(), text.size()));
    }

    /// @brief Verifies existence, kind, missing-entry, and invalid-path query behavior.
    void testBasicEntryQueries(TestSupport::Context &context, const std::filesystem::path &root)
    {
        const std::filesystem::path directory = root / "basic" / "directory";
        const std::filesystem::path file = root / "basic" / "file.txt";
        const std::filesystem::path missing = root / "basic" / "missing.txt";

        static_cast<void>(context.expectTrue("basic fixture directory creation succeeds", TestSupport::createDirectories(directory).ok()));
        static_cast<void>(context.expectTrue("basic fixture file write succeeds", TestSupport::writeTextFile(file, "payload").ok()));

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
                FileSystem::Types::FileReaderOpenOptions{.symlinkPolicy = FileSystem::Types::SymlinkPolicy::DoNotFollow});
            static_cast<void>(context.expectEq("DoNotFollow file open rejects final symlink", ErrorCode::InvalidArgument, strictReaderOpen.code));

            FileSystem::FileReader finalReader;
            const IO::Types::Status finalReaderOpen = finalReader.open(
                finalFileLink,
                FileSystem::Types::FileReaderOpenOptions{.symlinkPolicy = FileSystem::Types::SymlinkPolicy::FollowFinal});
            static_cast<void>(context.expectTrue("FollowFinal file open reaches final symlink target", finalReaderOpen.ok()));
            static_cast<void>(context.expectTrue("FollowFinal final symlink reader closes", finalReader.close().ok()));

            const IO::Types::Status removeFinalLinkTarget = FileSystem::removeFile(
                finalFileLink,
                FileSystem::Types::RemoveOptions{.symlinkPolicy = FileSystem::Types::SymlinkPolicy::FollowFinal});
            static_cast<void>(
                context.expectEq("FollowFinal remove final symlink is unsupported", ErrorCode::Unsupported, removeFinalLinkTarget.code));

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
            static_cast<void>(context.expectEq(
                "FollowFinal rejects intermediate symlink",
                ErrorCode::PermissionDenied,
                followFinalThroughIntermediate.status.code));

            const IO::Types::WriteResult strictWrite = FileSystem::writeAllText(
                pathThroughIntermediate,
                "blocked",
                FileSystem::Types::WriteFileOptions{.symlinkPolicy = FileSystem::Types::SymlinkPolicy::DoNotFollow});
            static_cast<void>(
                context.expectEq("DoNotFollow write rejects intermediate symlink", ErrorCode::PermissionDenied, strictWrite.status.code));

            const auto strictListing = FileSystem::listDirectory(
                intermediateDirectoryLink,
                FileSystem::Types::ListDirectoryOptions{.symlinkPolicy = FileSystem::Types::SymlinkPolicy::DoNotFollow});
            static_cast<void>(context.expectEq("DoNotFollow listDirectory rejects symlink root", ErrorCode::NotDirectory, strictListing.status.code));
        }
    }

    /// @brief Verifies metadata queries, directory creation, listing filters, and path helpers.
    void testMetadataDirectoriesAndListing(TestSupport::Context &context, const std::filesystem::path &root)
    {
        const std::filesystem::path directory = root / "directories" / "nested";
        const std::filesystem::path file = directory / "file.txt";

        const IO::Types::Status createStatus = FileSystem::createDirectories(directory);
        static_cast<void>(context.expectTrue("createDirectories succeeds", createStatus.ok()));

        const IO::Types::WriteResult writeStatus = FileSystem::writeAllText(file, "payload");
        static_cast<void>(context.expectTrue("writeAllText creates file", writeStatus.status.ok()));
        static_cast<void>(context.expectEq("writeAllText reports payload size", std::size_t{7}, writeStatus.bytesWritten));

        const IO::Types::SizeResult size = FileSystem::getFileSize(file);
        static_cast<void>(context.expectTrue("getFileSize succeeds", size.status.ok()));
        static_cast<void>(context.expectEq("getFileSize reports bytes", std::uint64_t{7}, size.sizeBytes));

        const auto lastWrite = FileSystem::getLastWriteTime(file);
        static_cast<void>(context.expectTrue("getLastWriteTime succeeds", lastWrite.status.ok()));

        const auto readOnly = FileSystem::isReadOnly(file);
        static_cast<void>(context.expectTrue("isReadOnly succeeds", readOnly.status.ok()));
        static_cast<void>(context.expectFalse("new file is writable", readOnly.value));

        static_cast<void>(context.expectTrue("setReadOnly true succeeds", FileSystem::setReadOnly(file, true).ok()));
        const auto readOnlyAfterSet = FileSystem::isReadOnly(file);
        static_cast<void>(context.expectTrue("isReadOnly after set succeeds", readOnlyAfterSet.status.ok()));
        static_cast<void>(context.expectTrue("setReadOnly true updates metadata", readOnlyAfterSet.value));
        static_cast<void>(context.expectTrue("setReadOnly false succeeds", FileSystem::setReadOnly(file, false).ok()));
        const auto writableAfterSet = FileSystem::isReadOnly(file);
        static_cast<void>(context.expectTrue("isReadOnly after clear succeeds", writableAfterSet.status.ok()));
        static_cast<void>(context.expectFalse("setReadOnly false updates metadata", writableAfterSet.value));

        const auto listing = FileSystem::listDirectory(directory);
        static_cast<void>(context.expectTrue("listDirectory succeeds", listing.status.ok()));
        static_cast<void>(context.expectEq("listDirectory returns one entry", std::size_t{1}, listing.entries.size()));
        if (!listing.entries.empty())
        {
            static_cast<void>(
                context.expectEq("listDirectory entry is file", FileSystem::Types::EntryKind::RegularFile, listing.entries[0].info.kind));
        }

        const auto limitedListing = FileSystem::listDirectory(directory, FileSystem::Types::ListDirectoryOptions{.maxEntries = 0});
        static_cast<void>(
            context.expectEq("listDirectory maxEntries returns SizeLimitExceeded", ErrorCode::SizeLimitExceeded, limitedListing.status.code));

        FileSystem::DirectoryCursor cursor;
        static_cast<void>(context.expectEq("closed directory cursor reports NotOpen", ErrorCode::NotOpen, cursor.next().status.code));
        static_cast<void>(context.expectTrue("directory cursor opens", cursor.open(directory).ok()));
        static_cast<void>(context.expectEq("open directory cursor rejects AlreadyOpen", ErrorCode::AlreadyOpen, cursor.open(directory).code));
        FileSystem::DirectoryCursor movedCursor = std::move(cursor);
        const auto cursorEntry = movedCursor.next();
        static_cast<void>(context.expectTrue("directory cursor returns an entry", cursorEntry.status.ok() && cursorEntry.hasEntry));
        const auto cursorEnd = movedCursor.next();
        static_cast<void>(context.expectTrue("directory cursor reports successful exhaustion", cursorEnd.status.ok() && !cursorEnd.hasEntry));
        static_cast<void>(context.expectTrue("directory cursor closes", movedCursor.close().ok()));
        static_cast<void>(context.expectFalse("closed directory cursor is not open", movedCursor.isOpen()));

        FileSystem::DirectoryCursor limitedCursor;
        static_cast<void>(context.expectTrue(
            "limited directory cursor opens",
            limitedCursor.open(directory, FileSystem::Types::ListDirectoryOptions{.maxEntries = 0}).ok()));
        static_cast<void>(context.expectEq("directory cursor enforces maxEntries", ErrorCode::SizeLimitExceeded, limitedCursor.next().status.code));
        static_cast<void>(
            context.expectEq("directory cursor keeps the terminal limit status", ErrorCode::SizeLimitExceeded, limitedCursor.next().status.code));
    }

    /// @brief Verifies explicit UTF-8 path conversion with non-ASCII fixture names.
    void testUtf8PathConversion(TestSupport::Context &context, const std::filesystem::path &root)
    {
        const std::string utf8Name = std::string{"unicode_"} + "\xD1\x84\xD0\xB0\xD0\xB9\xD0\xBB.txt";
        const auto namePath = FileSystem::pathFromUtf8(utf8Name);
        static_cast<void>(context.expectTrue("pathFromUtf8 succeeds for non-ASCII filename", namePath.status.ok()));

        const std::filesystem::path path = root / "unicode" / namePath.path;
        static_cast<void>(context.expectTrue("write non-ASCII path succeeds", FileSystem::writeAllText(path, "text").status.ok()));
        const auto roundTrip = FileSystem::pathToUtf8(path.filename());
        static_cast<void>(context.expectTrue("pathToUtf8 succeeds for non-ASCII filename", roundTrip.status.ok()));
        static_cast<void>(context.expectEq("UTF-8 path round-trips", utf8Name, roundTrip.utf8));
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
            FileSystem::Types::FileOpenOptions{
                .access = FileSystem::Types::FileAccess::ReadWrite,
                .mode = FileSystem::Types::FileOpenMode::OpenExisting,
                .initialPosition = FileSystem::Types::FileInitialPosition::End});
        static_cast<void>(context.expectTrue("File open succeeds", openStatus.ok()));

        const IO::Types::PositionResult endPosition = fileHandle.position();
        static_cast<void>(context.expectTrue("File position succeeds", endPosition.status.ok()));
        static_cast<void>(context.expectEq("FileInitialPosition::End seeks to EOF", std::uint64_t{6}, endPosition.position));

        const IO::Types::WriteResult handleWrite = fileHandle.write(bytesOf("g"));
        static_cast<void>(context.expectTrue("File write succeeds", handleWrite.status.ok()));
        static_cast<void>(context.expectEq("File write byte count", std::size_t{1}, handleWrite.bytesWritten));
        static_cast<void>(context.expectTrue("File close succeeds", fileHandle.close().ok()));

        const IO::Types::ReadAllTextResult updated = FileSystem::readAllText(file);
        static_cast<void>(context.expectTrue("readAllText after File write succeeds", updated.status.ok()));
        static_cast<void>(context.expectEq("File write updated content", std::string{"abcdefg"}, updated.text));

        FileSystem::FileWriter appendWriter;
        const IO::Types::Status appendOpen =
            appendWriter.open(file, FileSystem::Types::FileWriterOpenOptions{.mode = FileSystem::Types::FileWriterMode::AppendExisting});
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
        FileSystem::Types::CopyFileOptions copyOptions;
        copyOptions.metadataMode = FileSystem::Types::CopyMetadataMode::Basic;
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

#if defined(_WIN32) && INTERNAL_FILESYSTEM_TEST_HOOKS
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
            static_cast<void>(
                context.expectTrue("anchored move lands in retained parent", FileSystem::exists(parkedParent / "destination.txt").value));
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
        static_cast<void>(context.expectEq(
            "removeDirectoryTree deep count includes root levels and leaf",
            kDeepDirectoryDepth + 2,
            removedDeepTree.removedEntries));

        const std::filesystem::path zeroLimitTree = directory / "zero-limit-tree";
        static_cast<void>(context.expectTrue("create zero-limit tree succeeds", FileSystem::createDirectories(zeroLimitTree).ok()));
        static_cast<void>(
            context.expectTrue("write zero-limit tree file succeeds", FileSystem::writeAllText(zeroLimitTree / "file.txt", "x").status.ok()));
        const auto zeroLimitRemoval = FileSystem::removeDirectoryTree(zeroLimitTree, FileSystem::Types::RemoveDirectoryTreeOptions{.maxEntries = 0});
        static_cast<void>(
            context.expectEq("zero remove limit reports SizeLimitExceeded", ErrorCode::SizeLimitExceeded, zeroLimitRemoval.status.code));
        static_cast<void>(context.expectEq("zero remove limit removes nothing", std::uint64_t{0}, zeroLimitRemoval.removedEntries));
        static_cast<void>(context.expectTrue("zero remove limit preserves root", FileSystem::isDirectory(zeroLimitTree).value));
        static_cast<void>(context.expectTrue("zero remove limit cleanup succeeds", FileSystem::removeDirectoryTree(zeroLimitTree).status.ok()));

        const std::filesystem::path exactLimitTree = directory / "exact-limit-tree";
        static_cast<void>(context.expectTrue("create exact-limit tree succeeds", FileSystem::createDirectories(exactLimitTree / "child").ok()));
        static_cast<void>(context.expectTrue(
            "write exact-limit tree file succeeds",
            FileSystem::writeAllText(exactLimitTree / "child" / "file.txt", "x").status.ok()));
        const auto exactLimitRemoval =
            FileSystem::removeDirectoryTree(exactLimitTree, FileSystem::Types::RemoveDirectoryTreeOptions{.maxEntries = 3});
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
            FileSystem::removeDirectoryTree(wideTree, FileSystem::Types::RemoveDirectoryTreeOptions{.maxEntries = kPartialRemovalLimit});
        static_cast<void>(
            context.expectEq("partial tree removal reports SizeLimitExceeded", ErrorCode::SizeLimitExceeded, partialRemoval.status.code));
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
            FileSystem::Types::AtomicWriteOptions{.replaceMode = FileSystem::Types::ReplaceMode::FailIfExists});
        static_cast<void>(context.expectEq("atomic fail-if-exists reports AlreadyExists", ErrorCode::AlreadyExists, atomicFailIfExists.code));
        static_cast<void>(context.expectEq("atomic fail-if-exists preserves content", std::string{"new"}, FileSystem::readAllText(file).text));

        FileSystem::File lockFile;
        const IO::Types::Status openStatus = lockFile.open(
            file,
            FileSystem::Types::FileOpenOptions{
                .access = FileSystem::Types::FileAccess::ReadWrite,
                .mode = FileSystem::Types::FileOpenMode::OpenExisting});
        static_cast<void>(context.expectTrue("lock file opens", openStatus.ok()));

        auto lock = lockFile.tryLockExclusive();
        static_cast<void>(context.expectTrue("exclusive lock call succeeds", lock.status.ok()));
        static_cast<void>(context.expectEq("exclusive lock acquired", FileSystem::Types::LockOutcome::Acquired, lock.outcome));

        FileSystem::File competingLockFile;
        const IO::Types::Status competingOpenStatus = competingLockFile.open(
            file,
            FileSystem::Types::FileOpenOptions{
                .access = FileSystem::Types::FileAccess::ReadWrite,
                .mode = FileSystem::Types::FileOpenMode::OpenExisting});
        static_cast<void>(context.expectTrue("competing lock file opens", competingOpenStatus.ok()));
        auto competingLock = competingLockFile.tryLockExclusive();
        static_cast<void>(context.expectTrue("competing exclusive lock call succeeds", competingLock.status.ok()));
        static_cast<void>(
            context.expectEq("competing exclusive lock would block", FileSystem::Types::LockOutcome::WouldBlock, competingLock.outcome));
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
            static_cast<void>(context.expectEq("detached owner acquires lock", FileSystem::Types::LockOutcome::Acquired, result.outcome));
            return std::move(result.lock);
        }();

        FileSystem::File detachedCompetitor;
        static_cast<void>(context.expectTrue("detached lock competitor opens", detachedCompetitor.open(detachedOwnerFile).ok()));
        auto blockedByDetachedOwner = detachedCompetitor.tryLockExclusive();
        static_cast<void>(context.expectEq(
            "lock remains active after source handle destruction",
            FileSystem::Types::LockOutcome::WouldBlock,
            blockedByDetachedOwner.outcome));
        static_cast<void>(context.expectTrue("detached owner unlock succeeds", detachedOwnerLock.unlock().ok()));
        auto acquiredAfterDetachedUnlock = detachedCompetitor.tryLockExclusive();
        static_cast<void>(context.expectEq(
            "competitor acquires after detached owner unlock",
            FileSystem::Types::LockOutcome::Acquired,
            acquiredAfterDetachedUnlock.outcome));
        static_cast<void>(context.expectTrue("detached competitor unlock succeeds", acquiredAfterDetachedUnlock.lock.unlock().ok()));
        static_cast<void>(context.expectTrue("detached competitor closes", detachedCompetitor.close().ok()));

#if INTERNAL_FILESYSTEM_TEST_HOOKS
        const std::filesystem::path failedUnlockFile = root / "atomic-locks" / "failed-unlock.txt";
        static_cast<void>(context.expectTrue("failed-unlock file write succeeds", FileSystem::writeAllText(failedUnlockFile, "lock").status.ok()));

        FileSystem::File failedUnlockOwner;
        FileSystem::File failedUnlockCompetitor;
        static_cast<void>(context.expectTrue("failed-unlock owner opens", failedUnlockOwner.open(failedUnlockFile).ok()));
        static_cast<void>(context.expectTrue("failed-unlock competitor opens", failedUnlockCompetitor.open(failedUnlockFile).ok()));

        {
            auto failedUnlockLock = failedUnlockOwner.tryLockExclusive();
            static_cast<void>(
                context.expectEq("failed-unlock owner acquires lock", FileSystem::Types::LockOutcome::Acquired, failedUnlockLock.outcome));

            auto blockedBeforeCleanup = failedUnlockCompetitor.tryLockExclusive();
            static_cast<void>(context.expectEq(
                "failed-unlock competitor initially blocks",
                FileSystem::Types::LockOutcome::WouldBlock,
                blockedBeforeCleanup.outcome));

            FileSystem::Detail::Platform::TestHooks::setFileUnlockFailure(true);
            FileSystem::FileLock lockDestroyedDuringFailure = std::move(failedUnlockLock.lock);
            static_cast<void>(context.expectTrue("failed-unlock injected lock remains active", lockDestroyedDuringFailure.active()));
        }
        FileSystem::Detail::Platform::TestHooks::reset();

        static_cast<void>(context.expectTrue("failed-unlock owner count clears after lock destruction", failedUnlockOwner.close().ok()));
        auto acquiredAfterFailedUnlockCleanup = failedUnlockCompetitor.tryLockExclusive();
        static_cast<void>(context.expectEq(
            "closing owner releases native lock after failed lock cleanup",
            FileSystem::Types::LockOutcome::Acquired,
            acquiredAfterFailedUnlockCleanup.outcome));
        static_cast<void>(
            context.expectTrue("failed-unlock competitor releases recovered lock", acquiredAfterFailedUnlockCleanup.lock.unlock().ok()));
        static_cast<void>(context.expectTrue("failed-unlock competitor closes", failedUnlockCompetitor.close().ok()));
#endif
    }
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

        TestSupport::Types::ReportOptions reportOptions;
        reportOptions.writeConsole = true;
        reportOptions.consoleVerbosity =
            options.verboseConsole ? TestSupport::Types::ConsoleVerbosity::Full : TestSupport::Types::ConsoleVerbosity::Minimal;
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

        const TestSupport::Types::Summary result = runner.result();
        runner.summary(std::format("FileSystem library self-tests passed={} failed={} skipped={}", result.passed, result.failed, result.skipped));

        return runner.exitCode();
    }
} // namespace GameWIP::Test
