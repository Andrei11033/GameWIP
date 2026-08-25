/// @file filesystem.cpp
/// @brief Platform-neutral FileSystem handles, validation, and whole-file operations.

#include "filesystem/filesystem.h"
#include "filesystem/internal/filesystem_platform.h"
#include "unicode/unicode.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <span>
#include <string>
#include <system_error>
#include <utility>

namespace GameWIP::FileSystem
{

    namespace
    {
        using ErrorCode = IO::Types::ErrorCode;

        /// @brief Builds a failed predicate result with a false payload.
        Types::BoolResult boolFailure(IO::Types::Status status) noexcept
        {
            return {.status = std::move(status), .value = false};
        }

        /// @brief Builds a failed predicate result from one portable error code.
        Types::BoolResult boolFailure(ErrorCode code) noexcept
        {
            return {.status = IO::makeStatus(code), .value = false};
        }

        /// @brief Builds a failed size result with a zero payload.
        IO::Types::SizeResult sizeFailure(IO::Types::Status status) noexcept
        {
            return {.status = std::move(status), .sizeBytes = 0};
        }

        /// @brief Builds a failed size result from one portable error code.
        IO::Types::SizeResult sizeFailure(ErrorCode code) noexcept
        {
            return {.status = IO::makeStatus(code), .sizeBytes = 0};
        }

        /// @brief Builds a failed timestamp result with a default time payload.
        Types::LastWriteTimeResult lastWriteTimeFailure(IO::Types::Status status) noexcept
        {
            return {.status = std::move(status), .time = Types::FileTime{}};
        }

        /// @brief Builds a failed timestamp result from one portable error code.
        Types::LastWriteTimeResult lastWriteTimeFailure(ErrorCode code) noexcept
        {
            return {.status = IO::makeStatus(code), .time = Types::FileTime{}};
        }

        /// @brief Builds a failed entry-info result with default metadata.
        Types::EntryInfoResult entryInfoFailure(IO::Types::Status status) noexcept
        {
            return {.status = std::move(status), .info = Types::EntryInfo{}};
        }

        /// @brief Builds a failed entry-info result from one portable error code.
        Types::EntryInfoResult entryInfoFailure(ErrorCode code) noexcept
        {
            return {.status = IO::makeStatus(code), .info = Types::EntryInfo{}};
        }

        /// @brief Builds a failed directory-list result with no entries.
        Types::Directory::ListResult listDirectoryFailure(IO::Types::Status status) noexcept
        {
            return {.status = std::move(status), .entries = {}};
        }

        /// @brief Builds a failed directory-list result from one portable error code.
        Types::Directory::ListResult listDirectoryFailure(ErrorCode code) noexcept
        {
            return {.status = IO::makeStatus(code), .entries = {}};
        }

        /// @brief Applies directory-list kind filters to one queried entry.
        bool includeDirectoryEntryKind(Types::EntryKind kind, const Types::Directory::ListOptions &options) noexcept
        {
            switch (kind)
            {
            case Types::EntryKind::RegularFile:
                return options.includeFiles;
            case Types::EntryKind::Directory:
                return options.includeDirectories;
            case Types::EntryKind::Symlink:
                return options.includeSymlinks;
            case Types::EntryKind::Other:
                return options.includeOther;
            }
            return false;
        }

        /// @brief Builds a failed tree-removal result while preserving completed removal progress.
        Types::Directory::RemoveTreeResult removeTreeFailure(IO::Types::Status status, std::uint64_t removedEntries = 0) noexcept
        {
            return {.status = std::move(status), .removedEntries = removedEntries};
        }

        /// @brief Builds a failed tree-removal result from a code and completed progress.
        Types::Directory::RemoveTreeResult removeTreeFailure(ErrorCode code, std::uint64_t removedEntries = 0) noexcept
        {
            return {.status = IO::makeStatus(code), .removedEntries = removedEntries};
        }

        /// @brief Builds a failed write result while preserving accepted payload progress.
        IO::Types::WriteResult writeFailure(IO::Types::Status status, std::size_t bytesWritten = 0) noexcept
        {
            return {.status = std::move(status), .bytesWritten = bytesWritten};
        }

        /// @brief Builds a failed write result from a code and accepted payload progress.
        IO::Types::WriteResult writeFailure(ErrorCode code, std::size_t bytesWritten = 0) noexcept
        {
            return {.status = IO::makeStatus(code), .bytesWritten = bytesWritten};
        }

        /// @brief Returns whether a character range is complete strict UTF-8.
        [[nodiscard]] bool isValidUtf8(std::string_view text) noexcept
        {
            return Unicode::Utf8::validate(text).outcome == Unicode::Types::ValidationOutcome::Valid;
        }

        /// @brief Validates a public query request before delegating to the platform backend.
        Detail::Platform::EntryQueryResult queryEntry(const Types::Path &path, Types::SymlinkPolicy symlinkPolicy)
        {
            if (path.empty())
            {
                return {.status = IO::makeStatus(ErrorCode::InvalidArgument)};
            }

            switch (symlinkPolicy)
            {
            case Types::SymlinkPolicy::DoNotFollow:
            case Types::SymlinkPolicy::FollowFinal:
            case Types::SymlinkPolicy::FollowAll:
                return Detail::Platform::queryEntry(path, symlinkPolicy);
            default:
                return {.status = IO::makeStatus(ErrorCode::InvalidArgument)};
            }
        }

        /// @brief Returns whether an owning wrapper contains live native file state.
        [[nodiscard]] bool stateIsOpen(const std::unique_ptr<Detail::FileState> &state) noexcept
        {
            return state != nullptr && state->nativeHandle != nullptr;
        }

        /// @brief Validates replace-mode enum values crossing the public boundary.
        [[nodiscard]] bool isValidReplaceMode(Types::ReplaceMode mode) noexcept
        {
            switch (mode)
            {
            case Types::ReplaceMode::FailIfExists:
            case Types::ReplaceMode::ReplaceExisting:
                return true;
            }

            return false;
        }

        /// @brief Rejects file-share bits outside the supported mask.
        [[nodiscard]] bool isValidFileShare(Types::File::Share share) noexcept
        {
            return (static_cast<std::uint8_t>(share) & ~static_cast<std::uint8_t>(Types::File::Share::All)) == 0;
        }

        /// @brief Validates symlink-policy enum values crossing the public boundary.
        [[nodiscard]] bool isValidSymlinkPolicy(Types::SymlinkPolicy policy) noexcept
        {
            switch (policy)
            {
            case Types::SymlinkPolicy::DoNotFollow:
            case Types::SymlinkPolicy::FollowFinal:
            case Types::SymlinkPolicy::FollowAll:
                return true;
            }

            return false;
        }

        /// @brief Validates file-access enum values crossing the public boundary.
        [[nodiscard]] bool isValidFileAccess(Types::File::Access access) noexcept
        {
            switch (access)
            {
            case Types::File::Access::Read:
            case Types::File::Access::Write:
            case Types::File::Access::ReadWrite:
                return true;
            }

            return false;
        }

        /// @brief Validates read/write file-open mode values.
        [[nodiscard]] bool isValidFileOpenMode(Types::File::OpenMode mode) noexcept
        {
            switch (mode)
            {
            case Types::File::OpenMode::OpenExisting:
            case Types::File::OpenMode::CreateNew:
            case Types::File::OpenMode::OpenOrCreate:
            case Types::File::OpenMode::TruncateExisting:
            case Types::File::OpenMode::CreateOrTruncate:
                return true;
            }

            return false;
        }

        /// @brief Validates write-only file-open mode values.
        [[nodiscard]] bool isValidFileWriterMode(Types::File::WriterMode mode) noexcept
        {
            switch (mode)
            {
            case Types::File::WriterMode::CreateNew:
            case Types::File::WriterMode::CreateOrTruncate:
            case Types::File::WriterMode::TruncateExisting:
            case Types::File::WriterMode::OpenOrCreate:
            case Types::File::WriterMode::AppendOrCreate:
            case Types::File::WriterMode::AppendExisting:
                return true;
            }

            return false;
        }

        /// @brief Validates initial-position enum values.
        [[nodiscard]] bool isValidInitialPosition(Types::File::InitialPosition position) noexcept
        {
            switch (position)
            {
            case Types::File::InitialPosition::Beginning:
            case Types::File::InitialPosition::End:
                return true;
            }

            return false;
        }

        /// @brief Validates metadata-copy policy values.
        [[nodiscard]] bool isValidCopyMetadataMode(Types::File::CopyMetadataMode mode) noexcept
        {
            switch (mode)
            {
            case Types::File::CopyMetadataMode::None:
            case Types::File::CopyMetadataMode::Basic:
                return true;
            }

            return false;
        }

        /// @brief Returns whether an access mode requests native write permission.
        [[nodiscard]] bool opensForWrite(Types::File::Access access) noexcept
        {
            return access == Types::File::Access::Write || access == Types::File::Access::ReadWrite;
        }

        /// @brief Returns whether an open mode can create or mutate file contents.
        [[nodiscard]] bool modeRequiresWrite(Types::File::OpenMode mode) noexcept
        {
            switch (mode)
            {
            case Types::File::OpenMode::OpenExisting:
                return false;
            case Types::File::OpenMode::CreateNew:
            case Types::File::OpenMode::OpenOrCreate:
            case Types::File::OpenMode::TruncateExisting:
            case Types::File::OpenMode::CreateOrTruncate:
                return true;
            default:
                return false;
            }
        }

        /// @brief Detects separators or embedded nulls forbidden in atomic temp prefixes.
        [[nodiscard]] bool hasPathSeparator(std::string_view text) noexcept
        {
            return text.find('/') != std::string_view::npos || text.find('\\') != std::string_view::npos || text.find('\0') != std::string_view::npos;
        }

        /// @brief Validates that an atomic temporary prefix is one safe filename component.
        [[nodiscard]] IO::Types::Status validateAtomicTemporaryPrefix(std::string_view prefix) noexcept
        {
            if (!isValidUtf8(prefix))
            {
                return IO::makeStatus(ErrorCode::EncodingFailed);
            }
            if (prefix.empty() || prefix == "." || prefix == ".." || hasPathSeparator(prefix))
            {
                return IO::makeStatus(ErrorCode::InvalidArgument);
            }

            return IO::successStatus();
        }

        /// @brief Requires an existing path to resolve to a directory under the requested policy.
        [[nodiscard]] IO::Types::Status validateDirectoryExists(const Types::Path &path, Types::SymlinkPolicy symlinkPolicy) noexcept
        {
            Detail::Platform::EntryQueryResult result = queryEntry(path, symlinkPolicy);
            if (!result.status.ok())
            {
                return std::move(result.status);
            }
            if (result.info.kind != Types::EntryKind::Directory)
            {
                return IO::makeStatus(ErrorCode::NotDirectory);
            }

            return IO::successStatus();
        }

        /// @brief Validates or creates a target's parent before opening a file.
        [[nodiscard]] IO::Types::Status validateParentDirectory(
            const Types::Path &path,
            bool createMissing,
            Types::SymlinkPolicy symlinkPolicy) noexcept
        {
            try
            {
                const Types::Path parent = path.parent_path();
                if (parent.empty())
                {
                    return IO::successStatus();
                }

                if (createMissing)
                {
                    return createDirectories(parent, Types::Directory::CreateOptions{.succeedIfAlreadyExists = true, .symlinkPolicy = symlinkPolicy});
                }

                return validateDirectoryExists(parent, symlinkPolicy);
            }
            catch (const std::bad_alloc &)
            {
                return IO::makeStatus(ErrorCode::OutOfMemory);
            }
            catch (...)
            {
                return IO::makeStatus(ErrorCode::Unknown);
            }
        }

        /// @brief Detects self-copy/self-move paths without requiring both paths to exist.
        /// @details Native equivalence is preferred for existing entries; lexical equality covers missing destinations and identical spellings.
        [[nodiscard]] bool equivalentOrSameLexically(const Types::Path &left, const Types::Path &right) noexcept
        {
            if (left == right)
            {
                return true;
            }

            std::error_code ec;
            const bool equivalent = std::filesystem::equivalent(left, right, ec);
            return !ec && equivalent;
        }

        /// @brief Performs a non-throwing final-component symlink check for guarded core paths.
        [[nodiscard]] bool finalEntryIsSymlink(const Types::Path &path) noexcept
        {
            const Detail::Platform::EntryQueryResult result = queryEntry(path, Types::SymlinkPolicy::DoNotFollow);
            return result.status.ok() && result.info.kind == Types::EntryKind::Symlink;
        }

        /// @brief Delegates portable metadata copying after core option validation.
        [[nodiscard]] IO::Types::Status copyBasicMetadata(const Types::Path &from, const Types::Path &to, Types::SymlinkPolicy symlinkPolicy) noexcept
        {
            return Detail::Platform::copyBasicMetadata(from, to, symlinkPolicy);
        }

        /// @brief Constructs a native path from UTF-8 already validated by the owning trust boundary.
        [[nodiscard]] Types::Path pathFromTrustedUtf8(std::string_view utf8Path)
        {
            std::u8string converted(utf8Path.size(), u8'\0');
            if (!utf8Path.empty())
            {
                std::ranges::transform(
                    utf8Path,
                    converted.begin(),
                    [](char byte)
                    {
                        return static_cast<char8_t>(byte);
                    });
            }
            return Types::Path{converted};
        }

        /// @brief Creates a process/counter/attempt-qualified same-directory atomic temp path.
        [[nodiscard]] Types::Path uniqueAtomicTemporaryPath(const Types::Path &parent, std::string_view prefix, std::uint64_t attempt)
        {
            static std::atomic<std::uint64_t> counter{0};

            const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
            const std::uint64_t value = counter.fetch_add(1, std::memory_order_relaxed);
            std::string name;
            name.reserve(prefix.size() + 64);
            name.append(prefix);
            name.append(std::to_string(static_cast<std::uint64_t>(ticks)));
            name.push_back('_');
            name.append(std::to_string(value));
            name.push_back('_');
            name.append(std::to_string(attempt));
            name.append(".tmp");

            return parent / pathFromTrustedUtf8(name);
        }

    } // namespace

#include "filesystem/core/filesystem_handles.inl"
#include "filesystem/core/filesystem_metadata.inl"
#include "filesystem/core/filesystem_whole_file.inl"
#include "filesystem/core/filesystem_directories.inl"
#include "filesystem/core/filesystem_mutations.inl"

} // namespace GameWIP::FileSystem
