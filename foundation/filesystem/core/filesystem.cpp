/// @file filesystem.cpp
/// @brief Platform-neutral FileSystem handles, validation, and whole-file operations.

#include "filesystem/filesystem.h"
#include "filesystem/internal/filesystem_platform.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <span>
#include <system_error>
#include <utility>

namespace GameWIP::FileSystem
{

    namespace
    {
        using ErrorCode = IO::Types::ErrorCode;

        /// @brief Builds a failed path result with an empty path payload.
        Types::PathResult pathFailure(IO::Types::Status status) noexcept
        {
            return {.status = std::move(status), .path = Types::Path{}};
        }

        /// @brief Builds a failed path result from one portable error code.
        Types::PathResult pathFailure(ErrorCode code) noexcept
        {
            return {.status = IO::makeStatus(code), .path = Types::Path{}};
        }

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

        /// @brief Builds a failed UTF-8 path result with empty text.
        Types::Utf8PathResult utf8Failure(ErrorCode code) noexcept
        {
            return {.status = IO::makeStatus(code), .utf8 = std::string{}};
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
        Types::ListDirectoryResult listDirectoryFailure(IO::Types::Status status) noexcept
        {
            return {.status = std::move(status), .entries = {}};
        }

        /// @brief Builds a failed directory-list result from one portable error code.
        Types::ListDirectoryResult listDirectoryFailure(ErrorCode code) noexcept
        {
            return {.status = IO::makeStatus(code), .entries = {}};
        }

        /// @brief Builds a failed tree-removal result while preserving completed removal progress.
        Types::RemoveDirectoryTreeResult removeTreeFailure(IO::Types::Status status, std::uint64_t removedEntries = 0) noexcept
        {
            return {.status = std::move(status), .removedEntries = removedEntries};
        }

        /// @brief Builds a failed tree-removal result from a code and completed progress.
        Types::RemoveDirectoryTreeResult removeTreeFailure(ErrorCode code, std::uint64_t removedEntries = 0) noexcept
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

        /// @brief Maps common standard-library filesystem errors to portable IO codes.
        IO::Types::Status statusFromStdError(std::error_code ec, ErrorCode fallback)
        {
            if (!ec)
            {
                return IO::successStatus();
            }

            ErrorCode code = fallback;

            if (ec == std::errc::no_such_file_or_directory)
            {
                code = ErrorCode::NotFound;
            }
            else if (ec == std::errc::not_a_directory)
            {
                code = ErrorCode::NotDirectory;
            }
            else if (ec == std::errc::permission_denied || ec == std::errc::operation_not_permitted)
            {
                code = ErrorCode::PermissionDenied;
            }
            else if (ec == std::errc::file_exists)
            {
                code = ErrorCode::AlreadyExists;
            }
            else if (ec == std::errc::filename_too_long)
            {
                code = ErrorCode::PathTooLong;
            }
            else if (ec == std::errc::no_space_on_device)
            {
                code = ErrorCode::StorageFull;
            }
            else if (ec == std::errc::device_or_resource_busy)
            {
                code = ErrorCode::ResourceBusy;
            }
            else if (ec == std::errc::interrupted)
            {
                code = ErrorCode::Interrupted;
            }
            else if (ec == std::errc::too_many_symbolic_link_levels)
            {
                code = ErrorCode::Unsupported;
            }
            else if (ec == std::errc::directory_not_empty)
            {
                code = ErrorCode::DirectoryNotEmpty;
            }

            return IO::makeStatus(code, ec.value(), ec.message());
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
        [[nodiscard]] bool isValidFileShare(Types::FileShare share) noexcept
        {
            return (static_cast<std::uint8_t>(share) & ~static_cast<std::uint8_t>(Types::FileShare::All)) == 0;
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
        [[nodiscard]] bool isValidFileAccess(Types::FileAccess access) noexcept
        {
            switch (access)
            {
            case Types::FileAccess::Read:
            case Types::FileAccess::Write:
            case Types::FileAccess::ReadWrite:
                return true;
            }

            return false;
        }

        /// @brief Validates read/write file-open mode values.
        [[nodiscard]] bool isValidFileOpenMode(Types::FileOpenMode mode) noexcept
        {
            switch (mode)
            {
            case Types::FileOpenMode::OpenExisting:
            case Types::FileOpenMode::CreateNew:
            case Types::FileOpenMode::OpenOrCreate:
            case Types::FileOpenMode::TruncateExisting:
            case Types::FileOpenMode::CreateOrTruncate:
                return true;
            }

            return false;
        }

        /// @brief Validates write-only file-open mode values.
        [[nodiscard]] bool isValidFileWriterMode(Types::FileWriterMode mode) noexcept
        {
            switch (mode)
            {
            case Types::FileWriterMode::CreateNew:
            case Types::FileWriterMode::CreateOrTruncate:
            case Types::FileWriterMode::TruncateExisting:
            case Types::FileWriterMode::OpenOrCreate:
            case Types::FileWriterMode::AppendOrCreate:
            case Types::FileWriterMode::AppendExisting:
                return true;
            }

            return false;
        }

        /// @brief Validates initial-position enum values.
        [[nodiscard]] bool isValidInitialPosition(Types::FileInitialPosition position) noexcept
        {
            switch (position)
            {
            case Types::FileInitialPosition::Beginning:
            case Types::FileInitialPosition::End:
                return true;
            }

            return false;
        }

        /// @brief Validates metadata-copy policy values.
        [[nodiscard]] bool isValidCopyMetadataMode(Types::CopyMetadataMode mode) noexcept
        {
            switch (mode)
            {
            case Types::CopyMetadataMode::None:
            case Types::CopyMetadataMode::Basic:
                return true;
            }

            return false;
        }

        /// @brief Returns whether an access mode requests native write permission.
        [[nodiscard]] bool opensForWrite(Types::FileAccess access) noexcept
        {
            return access == Types::FileAccess::Write || access == Types::FileAccess::ReadWrite;
        }

        /// @brief Returns whether an open mode can create or mutate file contents.
        [[nodiscard]] bool modeRequiresWrite(Types::FileOpenMode mode) noexcept
        {
            switch (mode)
            {
            case Types::FileOpenMode::OpenExisting:
                return false;
            case Types::FileOpenMode::CreateNew:
            case Types::FileOpenMode::OpenOrCreate:
            case Types::FileOpenMode::TruncateExisting:
            case Types::FileOpenMode::CreateOrTruncate:
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
            if (prefix.empty() || prefix == "." || prefix == ".." || hasPathSeparator(prefix))
            {
                return IO::makeStatus(ErrorCode::InvalidArgument);
            }

            return IO::successStatus();
        }

        /// @brief Requires an existing path to resolve to a directory under the requested policy.
        [[nodiscard]] IO::Types::Status validateDirectoryExists(const Types::Path &path, Types::SymlinkPolicy symlinkPolicy) noexcept
        {
            const Detail::Platform::EntryQueryResult result = queryEntry(path, symlinkPolicy);
            if (!result.status.ok())
            {
                return result.status;
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
                    return createDirectories(parent, Types::CreateDirectoryOptions{.succeedIfAlreadyExists = true, .symlinkPolicy = symlinkPolicy});
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

        /// @brief Detects source/destination identity using native equivalence with lexical fallback.
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

            return parent / name;
        }

    } // namespace

    FileLock::FileLock() noexcept = default;

    FileLock::FileLock(FileLock &&other) noexcept
        : state_(std::move(other.state_))
        , mode_(other.mode_)
    {
    }

    FileLock::~FileLock() noexcept
    {
        static_cast<void>(unlock());
    }

    bool FileLock::active() const noexcept
    {
        return state_ != nullptr && state_->active;
    }

    Types::FileLockMode FileLock::mode() const noexcept
    {
        return mode_;
    }

    IO::Types::Status FileLock::unlock() noexcept
    {
        if (!active())
        {
            state_.reset();
            return IO::successStatus();
        }

        IO::Types::Status status = Detail::Platform::unlockFile(*state_);
        if (status.ok())
        {
            state_.reset();
        }
        return status;
    }

    FileLock::FileLock(std::unique_ptr<Detail::FileLockState> state, Types::FileLockMode mode) noexcept
        : state_(std::move(state))
        , mode_(mode)
    {
    }

    FileReader::FileReader() noexcept = default;

    FileReader::FileReader(FileReader &&other) noexcept
        : IO::Reader(std::move(other))
        , state_(std::move(other.state_))
    {
    }

    FileReader::~FileReader() noexcept
    {
        static_cast<void>(close());
    }

    IO::Types::Status FileReader::open(const Types::Path &path, const Types::FileReaderOpenOptions &options) noexcept
    {
        if (isOpen())
        {
            return IO::makeStatus(ErrorCode::AlreadyOpen);
        }
        if (!isValidFileShare(options.share) || !isValidSymlinkPolicy(options.symlinkPolicy))
        {
            return IO::makeStatus(ErrorCode::InvalidArgument);
        }

        std::unique_ptr<Detail::FileState> newState;
        const IO::Types::Status status = Detail::Platform::openReader(newState, path, options);
        if (status.ok())
        {
            state_ = std::move(newState);
        }
        return status;
    }

    bool FileReader::isOpen() const noexcept
    {
        return stateIsOpen(state_);
    }

    bool FileReader::canSeek() const noexcept
    {
        return isOpen();
    }

    IO::Types::ReadResult FileReader::read(std::span<std::byte> destination) noexcept
    {
        if (!isOpen())
        {
            return {.status = IO::makeStatus(ErrorCode::NotOpen)};
        }

        return Detail::Platform::readFile(*state_, destination);
    }

    IO::Types::Status FileReader::close() noexcept
    {
        if (!isOpen())
        {
            state_.reset();
            return IO::successStatus();
        }

        IO::Types::Status status = Detail::Platform::closeFile(*state_);
        if (status.ok())
        {
            state_.reset();
        }
        return status;
    }

    IO::Types::PositionResult FileReader::position() const noexcept
    {
        if (!isOpen())
        {
            return {.status = IO::makeStatus(ErrorCode::NotOpen)};
        }

        return Detail::Platform::filePosition(*state_);
    }

    IO::Types::SizeResult FileReader::size() const noexcept
    {
        if (!isOpen())
        {
            return sizeFailure(ErrorCode::NotOpen);
        }

        return Detail::Platform::fileSize(*state_);
    }

    IO::Types::Status FileReader::seek(std::int64_t offset, IO::Types::SeekOrigin origin) noexcept
    {
        if (!isOpen())
        {
            return IO::makeStatus(ErrorCode::NotOpen);
        }

        return Detail::Platform::seekFile(*state_, offset, origin);
    }

    Types::LockResult FileReader::tryLockShared() noexcept
    {
        if (!isOpen())
        {
            return {.status = IO::makeStatus(ErrorCode::NotOpen)};
        }

        Detail::Platform::NativeLockResult result = Detail::Platform::tryLockFile(*state_, Types::FileLockMode::Shared);
        if (!result.status.ok() || result.outcome == Types::LockOutcome::WouldBlock)
        {
            return {.status = std::move(result.status), .outcome = result.outcome};
        }

        return {
            .status = IO::successStatus(),
            .outcome = Types::LockOutcome::Acquired,
            .lock = FileLock(std::move(result.state), Types::FileLockMode::Shared)};
    }

    FileWriter::FileWriter() noexcept = default;

    FileWriter::FileWriter(FileWriter &&other) noexcept
        : IO::Writer(std::move(other))
        , state_(std::move(other.state_))
    {
    }

    FileWriter::~FileWriter() noexcept
    {
        static_cast<void>(close());
    }

    IO::Types::Status FileWriter::open(const Types::Path &path, const Types::FileWriterOpenOptions &options) noexcept
    {
        if (isOpen())
        {
            return IO::makeStatus(ErrorCode::AlreadyOpen);
        }
        if (!isValidFileWriterMode(options.mode) || !isValidFileShare(options.share) || !isValidSymlinkPolicy(options.symlinkPolicy) ||
            !IO::isValidFlushMode(options.flushOnClose))
        {
            return IO::makeStatus(ErrorCode::InvalidArgument);
        }

        const IO::Types::Status parentStatus = validateParentDirectory(path, options.createParentDirectories, options.symlinkPolicy);
        if (!parentStatus.ok())
        {
            return parentStatus;
        }

        std::unique_ptr<Detail::FileState> newState;
        const IO::Types::Status status = Detail::Platform::openWriter(newState, path, options);
        if (status.ok())
        {
            state_ = std::move(newState);
        }
        return status;
    }

    bool FileWriter::isOpen() const noexcept
    {
        return stateIsOpen(state_);
    }

    bool FileWriter::canSeek() const noexcept
    {
        return isOpen() && !state_->appendMode;
    }

    IO::Types::WriteResult FileWriter::write(std::span<const std::byte> bytes) noexcept
    {
        if (!isOpen())
        {
            return writeFailure(ErrorCode::NotOpen);
        }

        return Detail::Platform::writeFile(*state_, bytes);
    }

    IO::Types::Status FileWriter::flush(IO::Types::FlushMode mode) noexcept
    {
        if (!isOpen())
        {
            return IO::makeStatus(ErrorCode::NotOpen);
        }

        return Detail::Platform::flushFile(*state_, mode);
    }

    IO::Types::Status FileWriter::close() noexcept
    {
        if (!isOpen())
        {
            state_.reset();
            return IO::successStatus();
        }

        IO::Types::Status status = Detail::Platform::closeFile(*state_);
        if (status.ok())
        {
            state_.reset();
        }
        return status;
    }

    IO::Types::PositionResult FileWriter::position() const noexcept
    {
        if (!isOpen())
        {
            return {.status = IO::makeStatus(ErrorCode::NotOpen)};
        }

        return Detail::Platform::filePosition(*state_);
    }

    IO::Types::SizeResult FileWriter::size() const noexcept
    {
        if (!isOpen())
        {
            return sizeFailure(ErrorCode::NotOpen);
        }

        return Detail::Platform::fileSize(*state_);
    }

    IO::Types::Status FileWriter::seek(std::int64_t offset, IO::Types::SeekOrigin origin) noexcept
    {
        if (!isOpen())
        {
            return IO::makeStatus(ErrorCode::NotOpen);
        }

        return Detail::Platform::seekFile(*state_, offset, origin);
    }

    Types::LockResult FileWriter::tryLockExclusive() noexcept
    {
        if (!isOpen())
        {
            return {.status = IO::makeStatus(ErrorCode::NotOpen)};
        }

        Detail::Platform::NativeLockResult result = Detail::Platform::tryLockFile(*state_, Types::FileLockMode::Exclusive);
        if (!result.status.ok() || result.outcome == Types::LockOutcome::WouldBlock)
        {
            return {.status = std::move(result.status), .outcome = result.outcome};
        }

        return {
            .status = IO::successStatus(),
            .outcome = Types::LockOutcome::Acquired,
            .lock = FileLock(std::move(result.state), Types::FileLockMode::Exclusive)};
    }

    File::File() noexcept = default;

    File::File(File &&other) noexcept
        : IO::Reader(std::move(static_cast<IO::Reader &>(other)))
        , IO::Writer(std::move(static_cast<IO::Writer &>(other)))
        , state_(std::move(other.state_))
    {
    }

    File::~File() noexcept
    {
        static_cast<void>(close());
    }

    IO::Types::Status File::open(const Types::Path &path, const Types::FileOpenOptions &options) noexcept
    {
        if (isOpen())
        {
            return IO::makeStatus(ErrorCode::AlreadyOpen);
        }
        if (!isValidFileAccess(options.access) || !isValidFileOpenMode(options.mode) || !isValidInitialPosition(options.initialPosition) ||
            !isValidFileShare(options.share) || !isValidSymlinkPolicy(options.symlinkPolicy) || !IO::isValidFlushMode(options.flushOnClose))
        {
            return IO::makeStatus(ErrorCode::InvalidArgument);
        }
        if (modeRequiresWrite(options.mode) && !opensForWrite(options.access))
        {
            return IO::makeStatus(ErrorCode::InvalidArgument);
        }
        if (options.flushOnClose != IO::Types::FlushMode::None && !opensForWrite(options.access))
        {
            return IO::makeStatus(ErrorCode::InvalidArgument);
        }

        const IO::Types::Status parentStatus = validateParentDirectory(path, options.createParentDirectories, options.symlinkPolicy);
        if (!parentStatus.ok())
        {
            return parentStatus;
        }

        std::unique_ptr<Detail::FileState> newState;
        const IO::Types::Status status = Detail::Platform::openFile(newState, path, options);
        if (status.ok())
        {
            state_ = std::move(newState);
        }
        return status;
    }

    bool File::isOpen() const noexcept
    {
        return stateIsOpen(state_);
    }

    bool File::canSeek() const noexcept
    {
        return isOpen();
    }

    Types::FileAccess File::access() const noexcept
    {
        return isOpen() ? state_->access : Types::FileAccess::ReadWrite;
    }

    IO::Types::ReadResult File::read(std::span<std::byte> destination) noexcept
    {
        if (!isOpen())
        {
            return {.status = IO::makeStatus(ErrorCode::NotOpen)};
        }

        return Detail::Platform::readFile(*state_, destination);
    }

    IO::Types::WriteResult File::write(std::span<const std::byte> bytes) noexcept
    {
        if (!isOpen())
        {
            return writeFailure(ErrorCode::NotOpen);
        }

        return Detail::Platform::writeFile(*state_, bytes);
    }

    IO::Types::Status File::flush(IO::Types::FlushMode mode) noexcept
    {
        if (!isOpen())
        {
            return IO::makeStatus(ErrorCode::NotOpen);
        }

        return Detail::Platform::flushFile(*state_, mode);
    }

    IO::Types::Status File::close() noexcept
    {
        if (!isOpen())
        {
            state_.reset();
            return IO::successStatus();
        }

        IO::Types::Status status = Detail::Platform::closeFile(*state_);
        if (status.ok())
        {
            state_.reset();
        }
        return status;
    }

    IO::Types::PositionResult File::position() const noexcept
    {
        if (!isOpen())
        {
            return {.status = IO::makeStatus(ErrorCode::NotOpen)};
        }

        return Detail::Platform::filePosition(*state_);
    }

    IO::Types::SizeResult File::size() const noexcept
    {
        if (!isOpen())
        {
            return sizeFailure(ErrorCode::NotOpen);
        }

        return Detail::Platform::fileSize(*state_);
    }

    IO::Types::Status File::seek(std::int64_t offset, IO::Types::SeekOrigin origin) noexcept
    {
        if (!isOpen())
        {
            return IO::makeStatus(ErrorCode::NotOpen);
        }

        return Detail::Platform::seekFile(*state_, offset, origin);
    }

    IO::Types::Status File::resize(std::uint64_t sizeBytes) noexcept
    {
        if (!isOpen())
        {
            return IO::makeStatus(ErrorCode::NotOpen);
        }

        return Detail::Platform::resizeFile(*state_, sizeBytes);
    }

    Types::LockResult File::tryLockShared() noexcept
    {
        if (!isOpen())
        {
            return {.status = IO::makeStatus(ErrorCode::NotOpen)};
        }

        Detail::Platform::NativeLockResult result = Detail::Platform::tryLockFile(*state_, Types::FileLockMode::Shared);
        if (!result.status.ok() || result.outcome == Types::LockOutcome::WouldBlock)
        {
            return {.status = std::move(result.status), .outcome = result.outcome};
        }

        return {
            .status = IO::successStatus(),
            .outcome = Types::LockOutcome::Acquired,
            .lock = FileLock(std::move(result.state), Types::FileLockMode::Shared)};
    }

    Types::LockResult File::tryLockExclusive() noexcept
    {
        if (!isOpen())
        {
            return {.status = IO::makeStatus(ErrorCode::NotOpen)};
        }

        Detail::Platform::NativeLockResult result = Detail::Platform::tryLockFile(*state_, Types::FileLockMode::Exclusive);
        if (!result.status.ok() || result.outcome == Types::LockOutcome::WouldBlock)
        {
            return {.status = std::move(result.status), .outcome = result.outcome};
        }

        return {
            .status = IO::successStatus(),
            .outcome = Types::LockOutcome::Acquired,
            .lock = FileLock(std::move(result.state), Types::FileLockMode::Exclusive)};
    }

    Types::BoolResult exists(const Types::Path &path, const Types::QueryOptions &options) noexcept
    {
        try
        {
            const Detail::Platform::EntryQueryResult result = queryEntry(path, options.symlinkPolicy);

            if (result.status.code == ErrorCode::NotFound)
            {
                return {.status = IO::successStatus(), .value = false};
            }
            if (!result.status.ok())
            {
                return boolFailure(result.status);
            }

            return {.status = IO::successStatus(), .value = true};
        }
        catch (const std::bad_alloc &)
        {
            return boolFailure(ErrorCode::OutOfMemory);
        }
        catch (...)
        {
            return boolFailure(ErrorCode::Unknown);
        }
    }

    Types::EntryInfoResult getEntryInfo(const Types::Path &path, const Types::QueryOptions &options) noexcept
    {
        try
        {
            Detail::Platform::EntryQueryResult result = queryEntry(path, options.symlinkPolicy);
            if (!result.status.ok())
            {
                return entryInfoFailure(std::move(result.status));
            }

            return {.status = IO::successStatus(), .info = result.info};
        }
        catch (const std::bad_alloc &)
        {
            return entryInfoFailure(ErrorCode::OutOfMemory);
        }
        catch (...)
        {
            return entryInfoFailure(ErrorCode::Unknown);
        }
    }

    Types::BoolResult isRegularFile(const Types::Path &path, const Types::QueryOptions &options) noexcept
    {
        try
        {
            const Detail::Platform::EntryQueryResult result = queryEntry(path, options.symlinkPolicy);
            if (result.status.code == ErrorCode::NotFound)
            {
                return {.status = IO::successStatus(), .value = false};
            }
            if (!result.status.ok())
            {
                return boolFailure(result.status);
            }
            return {.status = IO::successStatus(), .value = result.info.kind == Types::EntryKind::RegularFile};
        }
        catch (const std::bad_alloc &)
        {
            return boolFailure(ErrorCode::OutOfMemory);
        }
        catch (...)
        {
            return boolFailure(ErrorCode::Unknown);
        }
    }

    Types::BoolResult isDirectory(const Types::Path &path, const Types::QueryOptions &options) noexcept
    {
        try
        {
            const Detail::Platform::EntryQueryResult result = queryEntry(path, options.symlinkPolicy);
            if (result.status.code == ErrorCode::NotFound)
            {
                return {.status = IO::successStatus(), .value = false};
            }
            if (!result.status.ok())
            {
                return boolFailure(result.status);
            }
            return {.status = IO::successStatus(), .value = result.info.kind == Types::EntryKind::Directory};
        }
        catch (const std::bad_alloc &)
        {
            return boolFailure(ErrorCode::OutOfMemory);
        }
        catch (...)
        {
            return boolFailure(ErrorCode::Unknown);
        }
    }

    Types::BoolResult isSymlink(const Types::Path &path, const Types::QueryOptions &options) noexcept
    {
        try
        {
            const Detail::Platform::EntryQueryResult result = queryEntry(path, options.symlinkPolicy);
            if (result.status.code == ErrorCode::NotFound)
            {
                return {.status = IO::successStatus(), .value = false};
            }
            if (!result.status.ok())
            {
                return boolFailure(result.status);
            }
            return {.status = IO::successStatus(), .value = result.info.kind == Types::EntryKind::Symlink};
        }
        catch (const std::bad_alloc &)
        {
            return boolFailure(ErrorCode::OutOfMemory);
        }
        catch (...)
        {
            return boolFailure(ErrorCode::Unknown);
        }
    }

    IO::Types::SizeResult getFileSize(const Types::Path &path, const Types::QueryOptions &options) noexcept
    {
        try
        {
            const Types::EntryInfoResult info = getEntryInfo(path, options);
            if (!info.status.ok())
            {
                return sizeFailure(info.status);
            }
            if (info.info.kind != Types::EntryKind::RegularFile || !info.info.hasSize)
            {
                return sizeFailure(ErrorCode::InvalidArgument);
            }

            return {.status = IO::successStatus(), .sizeBytes = info.info.sizeBytes};
        }
        catch (const std::bad_alloc &)
        {
            return sizeFailure(ErrorCode::OutOfMemory);
        }
        catch (...)
        {
            return sizeFailure(ErrorCode::Unknown);
        }
    }

    Types::LastWriteTimeResult getLastWriteTime(const Types::Path &path, const Types::QueryOptions &options) noexcept
    {
        try
        {
            const Types::EntryInfoResult info = getEntryInfo(path, options);
            if (!info.status.ok())
            {
                return lastWriteTimeFailure(info.status);
            }
            if (!info.info.hasLastWriteTime)
            {
                return lastWriteTimeFailure(ErrorCode::StatFailed);
            }

            return {.status = IO::successStatus(), .time = info.info.lastWriteTime};
        }
        catch (const std::bad_alloc &)
        {
            return lastWriteTimeFailure(ErrorCode::OutOfMemory);
        }
        catch (...)
        {
            return lastWriteTimeFailure(ErrorCode::Unknown);
        }
    }

    Types::BoolResult isReadOnly(const Types::Path &path, const Types::QueryOptions &options) noexcept
    {
        try
        {
            const Types::EntryInfoResult info = getEntryInfo(path, options);
            if (!info.status.ok())
            {
                return boolFailure(info.status);
            }

            return {.status = IO::successStatus(), .value = info.info.readOnly};
        }
        catch (const std::bad_alloc &)
        {
            return boolFailure(ErrorCode::OutOfMemory);
        }
        catch (...)
        {
            return boolFailure(ErrorCode::Unknown);
        }
    }

    IO::Types::ReadAllBytesResult readAllBytes(const Types::Path &path, const Types::ReadFileOptions &options) noexcept
    {
        try
        {
            if (options.bufferSize == 0)
            {
                return {.status = IO::makeStatus(ErrorCode::InvalidArgument)};
            }

            FileReader reader;
            const IO::Types::Status openStatus = reader.open(path, options.open);
            if (!openStatus.ok())
            {
                return {.status = openStatus};
            }

            IO::Types::ReadAllBytesResult result = IO::readAllBytes(reader, options.maxBytes, options.bufferSize);
            const IO::Types::Status closeStatus = reader.close();
            if (result.status.ok() && !closeStatus.ok())
            {
                result.status = closeStatus;
            }
            return result;
        }
        catch (const std::bad_alloc &)
        {
            return {.status = IO::makeStatus(ErrorCode::OutOfMemory)};
        }
        catch (...)
        {
            return {.status = IO::makeStatus(ErrorCode::Unknown)};
        }
    }

    IO::Types::ReadAllTextResult readAllText(const Types::Path &path, const Types::ReadFileOptions &options) noexcept
    {
        try
        {
            if (options.bufferSize == 0)
            {
                return {.status = IO::makeStatus(ErrorCode::InvalidArgument)};
            }

            FileReader reader;
            const IO::Types::Status openStatus = reader.open(path, options.open);
            if (!openStatus.ok())
            {
                return {.status = openStatus};
            }

            IO::Types::ReadAllTextResult result = IO::readAllText(reader, options.maxBytes, options.bufferSize);
            const IO::Types::Status closeStatus = reader.close();
            if (result.status.ok() && !closeStatus.ok())
            {
                result.status = closeStatus;
            }
            return result;
        }
        catch (const std::bad_alloc &)
        {
            return {.status = IO::makeStatus(ErrorCode::OutOfMemory)};
        }
        catch (...)
        {
            return {.status = IO::makeStatus(ErrorCode::Unknown)};
        }
    }

    IO::Types::WriteResult writeAllBytes(const Types::Path &path, std::span<const std::byte> bytes, const Types::WriteFileOptions &options) noexcept
    {
        try
        {
            if (!IO::isValidFlushMode(options.flushMode) || !isValidFileShare(options.share) || !isValidSymlinkPolicy(options.symlinkPolicy))
            {
                return writeFailure(ErrorCode::InvalidArgument);
            }

            Types::FileWriterMode writerMode = Types::FileWriterMode::CreateOrTruncate;
            switch (options.mode)
            {
            case Types::WriteFileMode::CreateNew:
                writerMode = Types::FileWriterMode::CreateNew;
                break;
            case Types::WriteFileMode::CreateOrTruncate:
                writerMode = Types::FileWriterMode::CreateOrTruncate;
                break;
            case Types::WriteFileMode::TruncateExisting:
                writerMode = Types::FileWriterMode::TruncateExisting;
                break;
            default:
                return writeFailure(ErrorCode::InvalidArgument);
            }

            FileWriter writer;
            const IO::Types::Status openStatus = writer.open(
                path,
                Types::FileWriterOpenOptions{
                    .mode = writerMode,
                    .share = options.share,
                    .symlinkPolicy = options.symlinkPolicy,
                    .createParentDirectories = options.createParentDirectories,
                    .flushOnClose = IO::Types::FlushMode::None});
            if (!openStatus.ok())
            {
                return writeFailure(openStatus);
            }

            IO::Types::WriteResult result = IO::writeAllBytes(writer, bytes);
            if (!result.status.ok())
            {
                static_cast<void>(writer.close());
                return result;
            }

            const IO::Types::Status flushStatus = writer.flush(options.flushMode);
            if (!flushStatus.ok())
            {
                result.status = flushStatus;
                return result;
            }

            const IO::Types::Status closeStatus = writer.close();
            if (!closeStatus.ok())
            {
                result.status = closeStatus;
            }
            return result;
        }
        catch (const std::bad_alloc &)
        {
            return writeFailure(ErrorCode::OutOfMemory);
        }
        catch (...)
        {
            return writeFailure(ErrorCode::Unknown);
        }
    }

    IO::Types::WriteResult writeAllText(const Types::Path &path, std::string_view utf8Text, const Types::WriteFileOptions &options) noexcept
    {
        return writeAllBytes(path, std::as_bytes(std::span<const char>(utf8Text.data(), utf8Text.size())), options);
    }

    IO::Types::WriteResult appendBytes(const Types::Path &path, std::span<const std::byte> bytes, const Types::AppendFileOptions &options) noexcept
    {
        try
        {
            if (!IO::isValidFlushMode(options.flushMode) || !isValidFileShare(options.share) || !isValidSymlinkPolicy(options.symlinkPolicy))
            {
                return writeFailure(ErrorCode::InvalidArgument);
            }

            Types::FileWriterMode writerMode = Types::FileWriterMode::AppendOrCreate;
            switch (options.mode)
            {
            case Types::AppendMode::AppendOrCreate:
                writerMode = Types::FileWriterMode::AppendOrCreate;
                break;
            case Types::AppendMode::AppendExisting:
                writerMode = Types::FileWriterMode::AppendExisting;
                break;
            default:
                return writeFailure(ErrorCode::InvalidArgument);
            }

            FileWriter writer;
            const IO::Types::Status openStatus = writer.open(
                path,
                Types::FileWriterOpenOptions{
                    .mode = writerMode,
                    .share = options.share,
                    .symlinkPolicy = options.symlinkPolicy,
                    .createParentDirectories = options.createParentDirectories,
                    .flushOnClose = IO::Types::FlushMode::None});
            if (!openStatus.ok())
            {
                return writeFailure(openStatus);
            }

            IO::Types::WriteResult result = IO::writeAllBytes(writer, bytes);
            if (!result.status.ok())
            {
                static_cast<void>(writer.close());
                return result;
            }

            const IO::Types::Status flushStatus = writer.flush(options.flushMode);
            if (!flushStatus.ok())
            {
                result.status = flushStatus;
                return result;
            }

            const IO::Types::Status closeStatus = writer.close();
            if (!closeStatus.ok())
            {
                result.status = closeStatus;
            }
            return result;
        }
        catch (const std::bad_alloc &)
        {
            return writeFailure(ErrorCode::OutOfMemory);
        }
        catch (...)
        {
            return writeFailure(ErrorCode::Unknown);
        }
    }

    IO::Types::WriteResult appendText(const Types::Path &path, std::string_view utf8Text, const Types::AppendFileOptions &options) noexcept
    {
        return appendBytes(path, std::as_bytes(std::span<const char>(utf8Text.data(), utf8Text.size())), options);
    }

    IO::Types::Status writeAllBytesAtomic(
        const Types::Path &path,
        std::span<const std::byte> bytes,
        const Types::AtomicWriteOptions &options) noexcept
    {
        try
        {
            if (path.empty() || !isValidReplaceMode(options.replaceMode) || !isValidSymlinkPolicy(options.symlinkPolicy) ||
                !IO::isValidFlushMode(options.flushMode))
            {
                return IO::makeStatus(ErrorCode::InvalidArgument);
            }

            const IO::Types::Status prefixStatus = validateAtomicTemporaryPrefix(options.temporaryNamePrefix);
            if (!prefixStatus.ok())
            {
                return prefixStatus;
            }

            Types::Path parent = path.parent_path();
            if (parent.empty())
            {
                parent = ".";
            }

            const IO::Types::Status parentStatus =
                options.createParentDirectories
                    ? createDirectories(parent, Types::CreateDirectoryOptions{.succeedIfAlreadyExists = true, .symlinkPolicy = options.symlinkPolicy})
                    : validateDirectoryExists(parent, options.symlinkPolicy);
            if (!parentStatus.ok())
            {
                return parentStatus;
            }

            const Detail::Platform::EntryQueryResult destination = queryEntry(path, options.symlinkPolicy);
            if (destination.status.ok() && options.replaceMode == Types::ReplaceMode::FailIfExists)
            {
                return IO::makeStatus(ErrorCode::AlreadyExists);
            }
            if (!destination.status.ok() && destination.status.code != ErrorCode::NotFound)
            {
                return destination.status;
            }
            if (destination.status.ok() && options.symlinkPolicy != Types::SymlinkPolicy::DoNotFollow && finalEntryIsSymlink(path))
            {
                return IO::makeStatus(ErrorCode::Unsupported);
            }

            Types::Path temporaryPath;
            IO::Types::Status openStatus = IO::makeStatus(ErrorCode::AlreadyExists);
            FileWriter writer;
            for (std::uint64_t attempt = 0; attempt < 64 && openStatus.code == ErrorCode::AlreadyExists; ++attempt)
            {
                temporaryPath = uniqueAtomicTemporaryPath(parent, options.temporaryNamePrefix, attempt);
                openStatus = writer.open(
                    temporaryPath,
                    Types::FileWriterOpenOptions{
                        .mode = Types::FileWriterMode::CreateNew,
                        .share = Types::FileShare::None,
                        .symlinkPolicy = options.symlinkPolicy,
                        .createParentDirectories = false,
                        .flushOnClose = IO::Types::FlushMode::None});
            }
            if (!openStatus.ok())
            {
                return openStatus;
            }

            const IO::Types::WriteResult writeResult = IO::writeAllBytes(writer, bytes);
            if (!writeResult.status.ok())
            {
                static_cast<void>(writer.close());
                static_cast<void>(removeFile(temporaryPath, Types::RemoveOptions{.succeedIfMissing = true}));
                return writeResult.status;
            }

            const IO::Types::Status flushStatus = writer.flush(options.flushMode);
            if (!flushStatus.ok())
            {
                static_cast<void>(writer.close());
                static_cast<void>(removeFile(temporaryPath, Types::RemoveOptions{.succeedIfMissing = true}));
                return flushStatus;
            }

            const IO::Types::Status closeStatus = writer.close();
            if (!closeStatus.ok())
            {
                static_cast<void>(removeFile(temporaryPath, Types::RemoveOptions{.succeedIfMissing = true}));
                return closeStatus;
            }

            const IO::Types::Status commitStatus = Detail::Platform::movePath(temporaryPath, path, options.replaceMode, options.symlinkPolicy);
            if (!commitStatus.ok())
            {
                static_cast<void>(removeFile(temporaryPath, Types::RemoveOptions{.succeedIfMissing = true}));
                return commitStatus;
            }

            if (options.flushParentDirectory)
            {
                const IO::Types::Status parentFlushStatus = Detail::Platform::flushDirectory(parent);
                if (!parentFlushStatus.ok())
                {
                    return parentFlushStatus;
                }
            }

            return IO::successStatus();
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

    IO::Types::Status writeAllTextAtomic(const Types::Path &path, std::string_view utf8Text, const Types::AtomicWriteOptions &options) noexcept
    {
        return writeAllBytesAtomic(path, std::as_bytes(std::span<const char>(utf8Text.data(), utf8Text.size())), options);
    }

    IO::Types::Status createDirectory(const Types::Path &path, const Types::CreateDirectoryOptions &options) noexcept
    {
        try
        {
            return Detail::Platform::createDirectory(path, options);
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

    IO::Types::Status createDirectories(const Types::Path &path, const Types::CreateDirectoryOptions &options) noexcept
    {
        try
        {
            return Detail::Platform::createDirectories(path, options);
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

    IO::Types::Status resizeFile(const Types::Path &path, std::uint64_t sizeBytes, const Types::MutationOptions &options) noexcept
    {
        try
        {
            File file;
            const IO::Types::Status openStatus = file.open(
                path,
                Types::FileOpenOptions{
                    .access = Types::FileAccess::Write,
                    .mode = Types::FileOpenMode::OpenExisting,
                    .symlinkPolicy = options.symlinkPolicy});
            if (!openStatus.ok())
            {
                return openStatus;
            }

            const IO::Types::Status resizeStatus = file.resize(sizeBytes);
            const IO::Types::Status closeStatus = file.close();
            if (!resizeStatus.ok())
            {
                return resizeStatus;
            }
            return closeStatus;
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

    IO::Types::Status truncateFile(const Types::Path &path, const Types::MutationOptions &options) noexcept
    {
        return resizeFile(path, 0, options);
    }

    Types::ListDirectoryResult listDirectory(const Types::Path &path, const Types::ListDirectoryOptions &options) noexcept
    {
        try
        {
            return Detail::Platform::listDirectory(path, options);
        }
        catch (const std::bad_alloc &)
        {
            return listDirectoryFailure(ErrorCode::OutOfMemory);
        }
        catch (...)
        {
            return listDirectoryFailure(ErrorCode::Unknown);
        }
    }

    IO::Types::Status setReadOnly(const Types::Path &path, bool readOnly, const Types::QueryOptions &options) noexcept
    {
        try
        {
            return Detail::Platform::setReadOnly(path, readOnly, options.symlinkPolicy);
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

    IO::Types::Status copyFile(const Types::Path &from, const Types::Path &to, const Types::CopyFileOptions &options) noexcept
    {
        try
        {
            if (from.empty() || to.empty() || !isValidReplaceMode(options.replaceMode) || !isValidSymlinkPolicy(options.symlinkPolicy) ||
                !isValidCopyMetadataMode(options.metadataMode) || !IO::isValidFlushMode(options.flushMode))
            {
                return IO::makeStatus(ErrorCode::InvalidArgument);
            }

            const Types::EntryInfoResult source = getEntryInfo(from, Types::QueryOptions{.symlinkPolicy = options.symlinkPolicy});
            if (!source.status.ok())
            {
                return source.status;
            }
            if (source.info.kind != Types::EntryKind::RegularFile)
            {
                return IO::makeStatus(ErrorCode::InvalidArgument);
            }
            if (equivalentOrSameLexically(from, to))
            {
                return IO::makeStatus(ErrorCode::InvalidArgument);
            }

            const Detail::Platform::EntryQueryResult destination = queryEntry(to, Types::SymlinkPolicy::DoNotFollow);
            if (destination.status.ok() && options.replaceMode == Types::ReplaceMode::FailIfExists)
            {
                return IO::makeStatus(ErrorCode::AlreadyExists);
            }
            if (!destination.status.ok() && destination.status.code != ErrorCode::NotFound)
            {
                return destination.status;
            }

            const IO::Types::Status parentStatus = validateParentDirectory(to, options.createParentDirectories, options.symlinkPolicy);
            if (!parentStatus.ok())
            {
                return parentStatus;
            }

            FileReader reader;
            const IO::Types::Status readerOpenStatus =
                reader.open(from, Types::FileReaderOpenOptions{.share = Types::FileShare::All, .symlinkPolicy = options.symlinkPolicy});
            if (!readerOpenStatus.ok())
            {
                return readerOpenStatus;
            }

            const Types::FileWriterMode writerMode = options.replaceMode == Types::ReplaceMode::ReplaceExisting
                                                         ? Types::FileWriterMode::CreateOrTruncate
                                                         : Types::FileWriterMode::CreateNew;
            FileWriter writer;
            const IO::Types::Status writerOpenStatus = writer.open(
                to,
                Types::FileWriterOpenOptions{
                    .mode = writerMode,
                    .share = Types::FileShare::None,
                    .symlinkPolicy = options.symlinkPolicy,
                    .createParentDirectories = false});
            if (!writerOpenStatus.ok())
            {
                static_cast<void>(reader.close());
                return writerOpenStatus;
            }

            std::vector<std::byte> buffer(IO::kDefaultBufferSize);
            std::uint64_t copiedBytes = 0;
            while (true)
            {
                IO::Types::ReadResult readResult = reader.read(std::span<std::byte>(buffer.data(), buffer.size()));
                if (!readResult.status.ok())
                {
                    static_cast<void>(writer.close());
                    static_cast<void>(reader.close());
                    return readResult.status;
                }

                if (readResult.bytesRead > 0)
                {
                    IO::Types::WriteResult writeResult = IO::writeAllBytes(writer, std::span<const std::byte>(buffer.data(), readResult.bytesRead));
                    if (!writeResult.status.ok())
                    {
                        static_cast<void>(writer.close());
                        static_cast<void>(reader.close());
                        return writeResult.status;
                    }
                    copiedBytes += writeResult.bytesWritten;
                }

                if (readResult.endOfStream)
                {
                    break;
                }
            }

            if (source.info.hasSize && copiedBytes != source.info.sizeBytes)
            {
                static_cast<void>(writer.close());
                static_cast<void>(reader.close());
                return IO::makeStatus(ErrorCode::CopyFailed);
            }

            const IO::Types::Status flushStatus = writer.flush(options.flushMode);
            const IO::Types::Status writerCloseStatus = writer.close();
            const IO::Types::Status readerCloseStatus = reader.close();
            if (!flushStatus.ok())
            {
                return flushStatus;
            }
            if (!writerCloseStatus.ok())
            {
                return writerCloseStatus;
            }
            if (!readerCloseStatus.ok())
            {
                return readerCloseStatus;
            }

            if (options.metadataMode == Types::CopyMetadataMode::Basic)
            {
                return copyBasicMetadata(from, to, options.symlinkPolicy);
            }

            return IO::successStatus();
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

    IO::Types::Status movePath(const Types::Path &from, const Types::Path &to, const Types::MoveOptions &options) noexcept
    {
        try
        {
            if (from.empty() || to.empty() || !isValidReplaceMode(options.replaceMode) || !isValidSymlinkPolicy(options.symlinkPolicy))
            {
                return IO::makeStatus(ErrorCode::InvalidArgument);
            }
            if (options.symlinkPolicy != Types::SymlinkPolicy::DoNotFollow && finalEntryIsSymlink(from))
            {
                return IO::makeStatus(ErrorCode::Unsupported);
            }

            const Types::EntryInfoResult source = getEntryInfo(from, Types::QueryOptions{.symlinkPolicy = options.symlinkPolicy});
            if (!source.status.ok())
            {
                return source.status;
            }
            if (equivalentOrSameLexically(from, to))
            {
                return IO::successStatus();
            }
            const Detail::Platform::EntryQueryResult destination = queryEntry(to, Types::SymlinkPolicy::DoNotFollow);
            if (destination.status.ok() && options.replaceMode == Types::ReplaceMode::FailIfExists)
            {
                return IO::makeStatus(ErrorCode::AlreadyExists);
            }
            if (!destination.status.ok() && destination.status.code != ErrorCode::NotFound)
            {
                return destination.status;
            }

            const IO::Types::Status parentStatus = validateParentDirectory(to, options.createParentDirectories, options.symlinkPolicy);
            if (!parentStatus.ok())
            {
                return parentStatus;
            }

            const IO::Types::Status moveStatus = Detail::Platform::movePath(from, to, options.replaceMode, options.symlinkPolicy);
            if (!moveStatus.ok())
            {
                return moveStatus;
            }

            const Detail::Platform::EntryQueryResult sourceAfter = queryEntry(from, Types::SymlinkPolicy::DoNotFollow);
            if (sourceAfter.status.ok())
            {
                return IO::makeStatus(ErrorCode::MoveFailed);
            }
            if (sourceAfter.status.code != ErrorCode::NotFound)
            {
                return sourceAfter.status;
            }

            const Detail::Platform::EntryQueryResult destinationAfter = queryEntry(to, Types::SymlinkPolicy::DoNotFollow);
            if (!destinationAfter.status.ok())
            {
                return destinationAfter.status;
            }

            return IO::successStatus();
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

    IO::Types::Status removeFile(const Types::Path &path, const Types::RemoveOptions &options) noexcept
    {
        try
        {
            if (path.empty() || !isValidSymlinkPolicy(options.symlinkPolicy))
            {
                return IO::makeStatus(ErrorCode::InvalidArgument);
            }
            if (options.symlinkPolicy != Types::SymlinkPolicy::DoNotFollow && finalEntryIsSymlink(path))
            {
                return IO::makeStatus(ErrorCode::Unsupported);
            }

            return Detail::Platform::removeFile(path, options);
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

    IO::Types::Status removeEmptyDirectory(const Types::Path &path, const Types::RemoveOptions &options) noexcept
    {
        try
        {
            if (path.empty() || !isValidSymlinkPolicy(options.symlinkPolicy))
            {
                return IO::makeStatus(ErrorCode::InvalidArgument);
            }
            if (options.symlinkPolicy != Types::SymlinkPolicy::DoNotFollow && finalEntryIsSymlink(path))
            {
                return IO::makeStatus(ErrorCode::Unsupported);
            }

            return Detail::Platform::removeEmptyDirectory(path, options);
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

    Types::RemoveDirectoryTreeResult removeDirectoryTree(const Types::Path &path, const Types::RemoveDirectoryTreeOptions &options) noexcept
    {
        try
        {
            if (path.empty() || !isValidSymlinkPolicy(options.symlinkPolicy))
            {
                return removeTreeFailure(ErrorCode::InvalidArgument);
            }

            const Detail::Platform::EntryQueryResult existing = queryEntry(path, options.symlinkPolicy);
            if (!existing.status.ok())
            {
                if (existing.status.code == ErrorCode::NotFound && options.succeedIfMissing)
                {
                    return {.status = IO::successStatus(), .removedEntries = 0};
                }
                return removeTreeFailure(existing.status);
            }
            if (existing.info.kind != Types::EntryKind::Directory)
            {
                return removeTreeFailure(ErrorCode::NotDirectory);
            }
            if (options.symlinkPolicy != Types::SymlinkPolicy::DoNotFollow && finalEntryIsSymlink(path))
            {
                return removeTreeFailure(ErrorCode::Unsupported);
            }

            struct PendingEntry
            {
                Types::Path path;
                Types::EntryInfo info{};
                bool childrenVisited = false;
            };

            std::vector<PendingEntry> pending;
            pending.push_back(PendingEntry{.path = path, .info = existing.info, .childrenVisited = false});

            std::uint64_t removedEntries = 0;
            while (!pending.empty())
            {
                PendingEntry current = std::move(pending.back());
                pending.pop_back();

                if (current.info.kind == Types::EntryKind::Directory && !current.childrenVisited)
                {
                    const Types::Path directoryPath = current.path;
                    current.childrenVisited = true;
                    pending.push_back(std::move(current));

                    Types::ListDirectoryResult children = listDirectory(
                        directoryPath,
                        Types::ListDirectoryOptions{
                            .includeFiles = true,
                            .includeDirectories = true,
                            .includeSymlinks = true,
                            .includeOther = true,
                            .includeHidden = true,
                            .symlinkPolicy = Types::SymlinkPolicy::DoNotFollow,
                            .maxEntries = kNoEntryLimit});
                    if (!children.status.ok())
                    {
                        return removeTreeFailure(children.status, removedEntries);
                    }

                    for (auto iterator = children.entries.rbegin(); iterator != children.entries.rend(); ++iterator)
                    {
                        pending.push_back(PendingEntry{.path = iterator->path, .info = iterator->info, .childrenVisited = false});
                    }
                    continue;
                }

                if (removedEntries >= options.maxEntries)
                {
                    return removeTreeFailure(ErrorCode::SizeLimitExceeded, removedEntries);
                }

                const IO::Types::Status removeStatus =
                    current.info.kind == Types::EntryKind::Directory
                        ? removeEmptyDirectory(
                              current.path,
                              Types::RemoveOptions{.succeedIfMissing = false, .symlinkPolicy = Types::SymlinkPolicy::DoNotFollow})
                        : removeFile(
                              current.path,
                              Types::RemoveOptions{.succeedIfMissing = false, .symlinkPolicy = Types::SymlinkPolicy::DoNotFollow});
                if (!removeStatus.ok())
                {
                    return removeTreeFailure(removeStatus, removedEntries);
                }
                ++removedEntries;
            }

            return {.status = IO::successStatus(), .removedEntries = removedEntries};
        }
        catch (const std::bad_alloc &)
        {
            return removeTreeFailure(ErrorCode::OutOfMemory);
        }
        catch (...)
        {
            return removeTreeFailure(ErrorCode::Unknown);
        }
    }

    Types::PathResult getCurrentDirectory() noexcept
    {
        try
        {
            std::error_code ec;
            Types::Path path = std::filesystem::current_path(ec);
            if (ec)
            {
                return pathFailure(statusFromStdError(ec, ErrorCode::StatFailed));
            }
            return {.status = IO::successStatus(), .path = path};
        }
        catch (const std::bad_alloc &)
        {
            return pathFailure(ErrorCode::OutOfMemory);
        }
        catch (...)
        {
            return pathFailure(ErrorCode::Unknown);
        }
    }

    IO::Types::Status setCurrentDirectory(const Types::Path &path) noexcept
    {
        try
        {
            if (path.empty())
            {
                return IO::makeStatus(ErrorCode::InvalidArgument);
            }

            std::error_code ec;
            std::filesystem::current_path(path, ec);
            return statusFromStdError(ec, ErrorCode::OpenFailed);
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

    Types::PathResult parentPath(const Types::Path &path) noexcept
    {
        try
        {
            return {.status = IO::successStatus(), .path = path.parent_path()};
        }
        catch (const std::bad_alloc &)
        {
            return pathFailure(ErrorCode::OutOfMemory);
        }
        catch (...)
        {
            return pathFailure(ErrorCode::InvalidArgument);
        }
    }

    Types::PathResult filename(const Types::Path &path) noexcept
    {
        try
        {
            return {.status = IO::successStatus(), .path = path.filename()};
        }
        catch (const std::bad_alloc &)
        {
            return pathFailure(ErrorCode::OutOfMemory);
        }
        catch (...)
        {
            return pathFailure(ErrorCode::InvalidArgument);
        }
    }

    Types::PathResult stem(const Types::Path &path) noexcept
    {
        try
        {
            return {.status = IO::successStatus(), .path = path.stem()};
        }
        catch (const std::bad_alloc &)
        {
            return pathFailure(ErrorCode::OutOfMemory);
        }
        catch (...)
        {
            return pathFailure(ErrorCode::InvalidArgument);
        }
    }

    Types::PathResult extension(const Types::Path &path) noexcept
    {
        try
        {
            return {.status = IO::successStatus(), .path = path.extension()};
        }
        catch (const std::bad_alloc &)
        {
            return pathFailure(ErrorCode::OutOfMemory);
        }
        catch (...)
        {
            return pathFailure(ErrorCode::InvalidArgument);
        }
    }

    Types::PathResult replaceExtension(const Types::Path &path, const Types::Path &newExtension) noexcept
    {
        try
        {
            Types::Path result = path;
            result.replace_extension(newExtension);

            return {.status = IO::successStatus(), .path = result};
        }
        catch (const std::bad_alloc &)
        {
            return pathFailure(ErrorCode::OutOfMemory);
        }
        catch (...)
        {
            return pathFailure(ErrorCode::InvalidArgument);
        }
    }

    Types::PathResult joinPath(const Types::Path &left, const Types::Path &right) noexcept
    {
        try
        {
            Types::Path result = left;
            result /= right;

            return {.status = IO::successStatus(), .path = result};
        }
        catch (const std::bad_alloc &)
        {
            return pathFailure(ErrorCode::OutOfMemory);
        }
        catch (...)
        {
            return pathFailure(ErrorCode::InvalidArgument);
        }
    }

    Types::BoolResult isAbsolutePath(const Types::Path &path) noexcept
    {
        try
        {
            return {.status = IO::successStatus(), .value = path.is_absolute()};
        }
        catch (const std::bad_alloc &)
        {
            return boolFailure(ErrorCode::OutOfMemory);
        }
        catch (...)
        {
            return boolFailure(ErrorCode::InvalidArgument);
        }
    }

    Types::BoolResult isRelativePath(const Types::Path &path) noexcept
    {
        try
        {
            return {.status = IO::successStatus(), .value = path.is_relative()};
        }
        catch (const std::bad_alloc &)
        {
            return boolFailure(ErrorCode::OutOfMemory);
        }
        catch (...)
        {
            return boolFailure(ErrorCode::InvalidArgument);
        }
    }

    Types::PathResult absolutePath(const Types::Path &path) noexcept
    {
        try
        {
            if (path.empty())
            {
                return pathFailure(ErrorCode::InvalidArgument);
            }

            std::error_code ec;
            Types::Path result = std::filesystem::absolute(path, ec);
            if (ec)
            {
                return pathFailure(statusFromStdError(ec, ErrorCode::StatFailed));
            }
            return {.status = IO::successStatus(), .path = result};
        }
        catch (const std::bad_alloc &)
        {
            return pathFailure(ErrorCode::OutOfMemory);
        }
        catch (...)
        {
            return pathFailure(ErrorCode::Unknown);
        }
    }

    Types::PathResult canonicalPath(const Types::Path &path) noexcept
    {
        try
        {
            if (path.empty())
            {
                return pathFailure(ErrorCode::InvalidArgument);
            }

            std::error_code ec;
            Types::Path result = std::filesystem::canonical(path, ec);
            if (ec)
            {
                return pathFailure(statusFromStdError(ec, ErrorCode::StatFailed));
            }
            return {.status = IO::successStatus(), .path = result};
        }
        catch (const std::bad_alloc &)
        {
            return pathFailure(ErrorCode::OutOfMemory);
        }
        catch (...)
        {
            return pathFailure(ErrorCode::Unknown);
        }
    }

    Types::PathResult weaklyCanonicalPath(const Types::Path &path) noexcept
    {
        try
        {
            if (path.empty())
            {
                return pathFailure(ErrorCode::InvalidArgument);
            }

            std::error_code ec;
            Types::Path result = std::filesystem::weakly_canonical(path, ec);
            if (ec)
            {
                return pathFailure(statusFromStdError(ec, ErrorCode::StatFailed));
            }
            return {.status = IO::successStatus(), .path = result};
        }
        catch (const std::bad_alloc &)
        {
            return pathFailure(ErrorCode::OutOfMemory);
        }
        catch (...)
        {
            return pathFailure(ErrorCode::Unknown);
        }
    }

    Types::PathResult getTemporaryDirectoryPath() noexcept
    {
        try
        {
            std::error_code ec;
            Types::Path result = std::filesystem::temp_directory_path(ec);
            if (ec)
            {
                return pathFailure(statusFromStdError(ec, ErrorCode::StatFailed));
            }
            return {.status = IO::successStatus(), .path = result};
        }
        catch (const std::bad_alloc &)
        {
            return pathFailure(ErrorCode::OutOfMemory);
        }
        catch (...)
        {
            return pathFailure(ErrorCode::Unknown);
        }
    }

    Types::PathResult pathFromUtf8(std::string_view utf8Path) noexcept
    {
        try
        {
            const char8_t *u8Data = reinterpret_cast<const char8_t *>(utf8Path.data());
            std::u8string_view u8View{u8Data, utf8Path.size()};
            Types::Path result{u8View};

            return {.status = IO::successStatus(), .path = result};
        }
        catch (const std::bad_alloc &)
        {
            return pathFailure(ErrorCode::OutOfMemory);
        }
        catch (...)
        {
            return pathFailure(ErrorCode::EncodingFailed);
        }
    }

    Types::Utf8PathResult pathToUtf8(const Types::Path &path) noexcept
    {
        try
        {
            std::u8string u8String = path.u8string();
            std::string utf8{u8String.begin(), u8String.end()};

            return {.status = IO::successStatus(), .utf8 = utf8};
        }
        catch (const std::bad_alloc &)
        {
            return utf8Failure(ErrorCode::OutOfMemory);
        }
        catch (...)
        {
            return utf8Failure(ErrorCode::EncodingFailed);
        }
    }
} // namespace GameWIP::FileSystem
