/// @file filesystem.h
/// @brief Public API for the FileSystem foundation library.

#pragma once

#include "io/io.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

/// @brief Platform-neutral local path, file, directory, metadata, and locking primitives.
namespace GameWIP::FileSystem
{
    /// @brief Default prefix used for same-directory temporary files created by atomic writes.
    /// @note Callers may replace the prefix through Types::AtomicWriteOptions; it is a filename prefix, not a path.
    inline constexpr std::string_view kAtomicTemporaryNamePrefix = ".gamewip_tmp_";

    /// @brief Sentinel entry limit meaning no caller-imposed listing or tree-removal limit.
    /// @note Backend, container, address-space, and resource limits still apply.
    inline constexpr std::uint64_t kNoEntryLimit = std::numeric_limits<std::uint64_t>::max();

    /// @cond INTERNAL_FILESYSTEM_DETAIL
    namespace Detail
    {
        struct FileState;
        struct FileLockState;
        struct DirectoryCursorState;
    } // namespace Detail
    /// @endcond

    class File;
    class FileReader;
    class FileWriter;
    class FileLock;

    /// @brief Passive FileSystem values, options, and results.
    namespace Types
    {
        /// @brief Project-wide filesystem path spelling.
        /// @details This is a naming alias, not a portable path-grammar or ABI abstraction over std::filesystem::path.
        using Path = std::filesystem::path;

        /// @brief Portable wall-clock representation used for filesystem timestamps.
        /// @note Native precision may be reduced; an unrepresentable value is reported as SizeLimitExceeded.
        using FileTime = std::chrono::system_clock::time_point;

        /// @brief Kind of existing filesystem entry.
        enum class EntryKind
        {
            /// @brief A regular file.
            RegularFile,
            /// @brief A directory.
            Directory,
            /// @brief A symbolic link or equivalent link-like entry.
            Symlink,
            /// @brief An existing entry without a more specific portable kind.
            Other
        };

        /// @brief Access requested for a read/write File handle.
        enum class FileAccess
        {
            /// @brief Open for reading only.
            Read,
            /// @brief Open for writing only.
            Write,
            /// @brief Open for both reading and writing.
            ReadWrite
        };

        /// @brief Bitmask controlling access allowed to other opens while a handle remains open.
        enum class FileShare : std::uint8_t
        {
            /// @brief Allow no other read, write, or delete opens where the backend can enforce it.
            None = 0,
            /// @brief Allow other handles to request read access.
            Read = 1U << 0U,
            /// @brief Allow other handles to request write access.
            Write = 1U << 1U,
            /// @brief Allow rename, removal, or replacement while this handle remains open.
            Delete = 1U << 2U,
            /// @brief Allow other read and write opens.
            ReadWrite = 0x03U,
            /// @brief Allow other read, write, and delete opens.
            All = 0x07U
        };

        /// @brief Combines two file-sharing flags.
        /// @param left First sharing mask.
        /// @param right Second sharing mask.
        /// @return Bitwise union of left and right.
        [[nodiscard]] constexpr FileShare operator|(FileShare left, FileShare right) noexcept
        {
            return static_cast<FileShare>(static_cast<std::uint8_t>(left) | static_cast<std::uint8_t>(right));
        }

        /// @brief Intersects two file-sharing flags.
        /// @param left First sharing mask.
        /// @param right Second sharing mask.
        /// @return Bitwise intersection of left and right.
        [[nodiscard]] constexpr FileShare operator&(FileShare left, FileShare right) noexcept
        {
            return static_cast<FileShare>(static_cast<std::uint8_t>(left) & static_cast<std::uint8_t>(right));
        }

        /// @brief Adds sharing flags to an existing mask.
        /// @param left Sharing mask to update.
        /// @param right Sharing flags to add.
        /// @return Reference to the updated left mask.
        constexpr FileShare &operator|=(FileShare &left, FileShare right) noexcept
        {
            left = left | right;
            return left;
        }

        /// @brief Retains only sharing flags present in both masks.
        /// @param left Sharing mask to update.
        /// @param right Sharing flags to retain.
        /// @return Reference to the updated left mask.
        constexpr FileShare &operator&=(FileShare &left, FileShare right) noexcept
        {
            left = left & right;
            return left;
        }

        /// @brief Creation and truncation behavior for File::open().
        enum class FileOpenMode
        {
            /// @brief Open only when the file already exists.
            OpenExisting,
            /// @brief Create only when the file does not already exist.
            CreateNew,
            /// @brief Open an existing file or create a missing file without truncating.
            OpenOrCreate,
            /// @brief Open an existing file and truncate it to zero bytes.
            TruncateExisting,
            /// @brief Create a missing file or truncate an existing file.
            CreateOrTruncate
        };

        /// @brief Initial position requested after opening a non-append file.
        enum class FileInitialPosition
        {
            /// @brief Position the handle at byte zero.
            Beginning,
            /// @brief Position the handle at the current end of the file.
            End
        };

        /// @brief Creation, truncation, and append behavior for FileWriter::open().
        enum class FileWriterMode
        {
            /// @brief Create only when the file does not already exist.
            CreateNew,
            /// @brief Create a missing file or truncate an existing file.
            CreateOrTruncate,
            /// @brief Open an existing file and truncate it to zero bytes.
            TruncateExisting,
            /// @brief Open an existing file or create a missing file without truncating.
            OpenOrCreate,
            /// @brief Append to an existing file or create a missing file.
            AppendOrCreate,
            /// @brief Append only when the file already exists.
            AppendExisting
        };

        /// @brief Creation and replacement behavior for exact-content whole-file writes.
        enum class WriteFileMode
        {
            /// @brief Create only when the file does not already exist.
            CreateNew,
            /// @brief Create a missing file or replace existing contents.
            CreateOrTruncate,
            /// @brief Replace contents only when the file already exists.
            TruncateExisting
        };

        /// @brief Creation behavior for append helpers.
        enum class AppendMode
        {
            /// @brief Append to an existing file or create a missing file.
            AppendOrCreate,
            /// @brief Append only when the file already exists.
            AppendExisting
        };

        /// @brief Destination replacement policy.
        enum class ReplaceMode
        {
            /// @brief Fail when the destination already exists.
            FailIfExists,
            /// @brief Replace an existing destination where supported.
            ReplaceExisting
        };

        /// @brief Symbolic-link resolution policy.
        enum class SymlinkPolicy
        {
            /// @brief Reject intermediate symlinks and do not dereference a final symlink.
            DoNotFollow,
            /// @brief Reject intermediate symlinks but follow a final symlink.
            FollowFinal,
            /// @brief Allow symlink resolution through the complete path.
            FollowAll
        };

        /// @brief Active whole-file lock mode.
        enum class FileLockMode
        {
            /// @brief Shared lock intended to coexist with other shared locks.
            Shared,
            /// @brief Exclusive lock intended to exclude other compatible locks.
            Exclusive
        };

        /// @brief Outcome of a non-blocking lock attempt.
        enum class LockOutcome
        {
            /// @brief The requested lock was acquired.
            Acquired,
            /// @brief Another lock prevented immediate acquisition.
            WouldBlock
        };

        /// @brief Metadata copied with file contents.
        enum class CopyMetadataMode
        {
            /// @brief Copy file contents only.
            None,
            /// @brief Copy portable basic metadata such as last-write time and read-only state where supported.
            Basic
        };

        /// @brief Options used by File::open().
        struct FileOpenOptions
        {
            /// @brief Requested read/write access.
            FileAccess access = FileAccess::ReadWrite;
            /// @brief Creation and truncation behavior.
            FileOpenMode mode = FileOpenMode::OpenExisting;
            /// @brief Initial handle position after a successful open.
            FileInitialPosition initialPosition = FileInitialPosition::Beginning;
            /// @brief Access allowed to other opens while this handle remains open.
            FileShare share = FileShare::All;
            /// @brief Symlink traversal policy used while resolving the path.
            SymlinkPolicy symlinkPolicy = SymlinkPolicy::DoNotFollow;
            /// @brief Create missing parent directories before opening.
            bool createParentDirectories = false;
            /// @brief Flush requested by explicit close() before releasing the handle.
            IO::Types::FlushMode flushOnClose = IO::Types::FlushMode::None;
        };

        /// @brief Options used by FileReader::open().
        struct FileReaderOpenOptions
        {
            /// @brief Access allowed to other opens while this reader remains open.
            FileShare share = FileShare::All;
            /// @brief Symlink traversal policy used while resolving the path.
            SymlinkPolicy symlinkPolicy = SymlinkPolicy::DoNotFollow;
        };

        /// @brief Options used by FileWriter::open().
        struct FileWriterOpenOptions
        {
            /// @brief Creation, truncation, or append behavior.
            FileWriterMode mode = FileWriterMode::CreateOrTruncate;
            /// @brief Access allowed to other opens while this writer remains open.
            FileShare share = FileShare::All;
            /// @brief Symlink traversal policy used while resolving the path.
            SymlinkPolicy symlinkPolicy = SymlinkPolicy::DoNotFollow;
            /// @brief Create missing parent directories before opening.
            bool createParentDirectories = false;
            /// @brief Flush requested by explicit close() before releasing the handle.
            IO::Types::FlushMode flushOnClose = IO::Types::FlushMode::None;
        };

        /// @brief Options used by whole-file read helpers.
        struct ReadFileOptions
        {
            /// @brief Options used to open the underlying reader.
            FileReaderOpenOptions open{};
            /// @brief Maximum accepted file size, or IO::kNoByteLimit for no caller limit.
            /// @note This is a hard limit, not a successful truncation request.
            std::uint64_t maxBytes = IO::kNoByteLimit;
            /// @brief Temporary transfer buffer size. Must be greater than zero.
            /// @note The buffer is internal to the call and does not limit the final result size.
            std::size_t bufferSize = IO::kDefaultBufferSize;
        };

        /// @brief Options used by exact-content whole-file write helpers.
        struct WriteFileOptions
        {
            /// @brief Creation and replacement behavior.
            WriteFileMode mode = WriteFileMode::CreateOrTruncate;
            /// @brief Access allowed to other opens while the target is open.
            FileShare share = FileShare::All;
            /// @brief Symlink traversal policy used while resolving the path.
            SymlinkPolicy symlinkPolicy = SymlinkPolicy::DoNotFollow;
            /// @brief Create missing parent directories before opening.
            bool createParentDirectories = true;
            /// @brief Flush requested after payload writing and before close.
            IO::Types::FlushMode flushMode = IO::Types::FlushMode::None;
        };

        /// @brief Options used by append helpers.
        struct AppendFileOptions
        {
            /// @brief Whether a missing target is created or reported as NotFound.
            AppendMode mode = AppendMode::AppendOrCreate;
            /// @brief Access allowed to other opens while the target is open.
            FileShare share = FileShare::All;
            /// @brief Symlink traversal policy used while resolving the path.
            SymlinkPolicy symlinkPolicy = SymlinkPolicy::DoNotFollow;
            /// @brief Create missing parent directories before opening.
            bool createParentDirectories = true;
            /// @brief Flush requested after payload writing and before close.
            IO::Types::FlushMode flushMode = IO::Types::FlushMode::None;
        };

        /// @brief Options used by atomic whole-file replacement.
        struct AtomicWriteOptions
        {
            /// @brief Create missing parent directories before creating the temporary file.
            bool createParentDirectories = true;
            /// @brief Destination replacement behavior at commit.
            ReplaceMode replaceMode = ReplaceMode::ReplaceExisting;
            /// @brief Symlink traversal policy used to resolve the destination.
            SymlinkPolicy symlinkPolicy = SymlinkPolicy::DoNotFollow;
            /// @brief Flush requested for the temporary file before commit.
            IO::Types::FlushMode flushMode = IO::Types::FlushMode::Data;
            /// @brief Flush the parent directory after replacement.
            bool flushParentDirectory = true;
            /// @brief Filename prefix used for a generated temporary file in the destination directory.
            /// @note Must be non-empty and contain no path separator, embedded NUL, or complete "." or ".." name.
            std::string temporaryNamePrefix{kAtomicTemporaryNamePrefix};
        };

        /// @brief Options shared by metadata queries and simple metadata mutation.
        struct QueryOptions
        {
            /// @brief Symlink traversal policy used by the operation.
            SymlinkPolicy symlinkPolicy = SymlinkPolicy::DoNotFollow;
        };

        /// @brief Options used by path-based file resizing and truncation.
        struct MutationOptions
        {
            /// @brief Symlink traversal policy used to resolve the file to mutate.
            SymlinkPolicy symlinkPolicy = SymlinkPolicy::DoNotFollow;
        };

        /// @brief Portable metadata for an existing filesystem entry.
        struct EntryInfo
        {
            /// @brief Portable entry kind.
            EntryKind kind = EntryKind::Other;
            /// @brief Entry size in bytes when hasSize is true.
            std::uint64_t sizeBytes = 0;
            /// @brief Whether sizeBytes is available and meaningful.
            bool hasSize = false;
            /// @brief Last-write time when hasLastWriteTime is true.
            FileTime lastWriteTime{};
            /// @brief Whether lastWriteTime is available and meaningful.
            bool hasLastWriteTime = false;
            /// @brief Portable basic read-only state.
            /// @note This is not a complete permissions, ownership, or ACL model.
            bool readOnly = false;
        };

        /// @brief Generic boolean query result.
        struct BoolResult
        {
            /// @brief Operation status.
            IO::Types::Status status;
            /// @brief Boolean result when status is successful.
            bool value = false;
        };

        /// @brief Result returned by getEntryInfo().
        struct EntryInfoResult
        {
            /// @brief Operation status.
            IO::Types::Status status;
            /// @brief Entry metadata when status is successful.
            EntryInfo info{};
        };

        /// @brief Result returned by getLastWriteTime().
        struct LastWriteTimeResult
        {
            /// @brief Operation status.
            IO::Types::Status status;
            /// @brief Last-write time when status is successful.
            FileTime time{};
        };

        /// @brief Result returned by path-producing operations.
        struct PathResult
        {
            /// @brief Operation status.
            IO::Types::Status status;
            /// @brief Resulting path when status is successful.
            Path path;
        };

        /// @brief Result returned when converting a Path to UTF-8.
        struct Utf8PathResult
        {
            /// @brief Operation status.
            IO::Types::Status status;
            /// @brief UTF-8 path text when status is successful.
            std::string utf8;
        };

        /// @brief Options used by createDirectory() and createDirectories().
        struct CreateDirectoryOptions
        {
            /// @brief Treat an already-existing directory as success.
            bool succeedIfAlreadyExists = true;
            /// @brief Symlink traversal policy used while resolving the path.
            SymlinkPolicy symlinkPolicy = SymlinkPolicy::DoNotFollow;
        };

        /// @brief Options used by listDirectory().
        struct ListDirectoryOptions
        {
            /// @brief Include regular files.
            bool includeFiles = true;
            /// @brief Include directories.
            bool includeDirectories = true;
            /// @brief Include symbolic links or equivalent link-like entries.
            bool includeSymlinks = true;
            /// @brief Include existing entries without another portable kind.
            bool includeOther = true;
            /// @brief Include entries considered hidden by the backend.
            bool includeHidden = true;
            /// @brief Symlink policy used when obtaining child metadata.
            SymlinkPolicy symlinkPolicy = SymlinkPolicy::DoNotFollow;
            /// @brief Maximum number of returned entries, or kNoEntryLimit for no caller limit.
            std::uint64_t maxEntries = kNoEntryLimit;
        };

        /// @brief One direct child returned by listDirectory().
        struct DirectoryEntry
        {
            /// @brief Supplied parent path joined with the child name.
            /// @note A relative parent produces a relative child path; this field is not necessarily absolute.
            Path path;
            /// @brief Portable metadata for the child.
            EntryInfo info{};
        };

        /// @brief Result returned by listDirectory().
        /// @details SizeLimitExceeded may return entries collected before the limit was reached. The result owns all
        /// accepted entries, so retained storage is proportional to their count and path sizes.
        struct ListDirectoryResult
        {
            /// @brief Operation status.
            IO::Types::Status status;
            /// @brief Direct children collected before completion or failure.
            std::vector<DirectoryEntry> entries;
        };

        /// @brief Result returned by one DirectoryCursor step.
        /// @details Successful exhaustion is represented by hasEntry=false. A failed result does not contain an entry.
        struct DirectoryCursorNextResult
        {
            /// @brief Operation status.
            IO::Types::Status status;
            /// @brief Direct child returned when hasEntry is true.
            DirectoryEntry entry;
            /// @brief Whether this step returned one accepted child.
            bool hasEntry = false;
        };

        /// @brief Options used by removeFile() and removeEmptyDirectory().
        struct RemoveOptions
        {
            /// @brief Treat a missing target as success.
            bool succeedIfMissing = false;
            /// @brief Symlink traversal policy used to select the entry to remove.
            SymlinkPolicy symlinkPolicy = SymlinkPolicy::DoNotFollow;
        };

        /// @brief Options used by removeDirectoryTree().
        struct RemoveDirectoryTreeOptions
        {
            /// @brief Treat a missing target as success.
            bool succeedIfMissing = false;
            /// @brief Symlink traversal policy used only for the initial path.
            SymlinkPolicy symlinkPolicy = SymlinkPolicy::DoNotFollow;
            /// @brief Maximum number of removed entries, or kNoEntryLimit for no caller limit.
            std::uint64_t maxEntries = kNoEntryLimit;
        };

        /// @brief Result returned by removeDirectoryTree().
        /// @details SizeLimitExceeded or another failure may report entries removed before the failure.
        struct RemoveDirectoryTreeResult
        {
            /// @brief Operation status.
            IO::Types::Status status;
            /// @brief Number of entries removed before completion or failure.
            std::uint64_t removedEntries = 0;
        };

        /// @brief Options used by movePath().
        struct MoveOptions
        {
            /// @brief Destination replacement behavior.
            ReplaceMode replaceMode = ReplaceMode::FailIfExists;
            /// @brief Symlink traversal policy used for source resolution and destination parent traversal.
            SymlinkPolicy symlinkPolicy = SymlinkPolicy::DoNotFollow;
            /// @brief Create missing destination parent directories before moving.
            bool createParentDirectories = false;
        };

        /// @brief Options used by copyFile().
        struct CopyFileOptions
        {
            /// @brief Destination replacement behavior.
            ReplaceMode replaceMode = ReplaceMode::FailIfExists;
            /// @brief Symlink traversal policy used for source resolution and destination path traversal.
            SymlinkPolicy symlinkPolicy = SymlinkPolicy::DoNotFollow;
            /// @brief Create missing destination parent directories before copying.
            bool createParentDirectories = false;
            /// @brief Additional metadata to copy after file contents.
            CopyMetadataMode metadataMode = CopyMetadataMode::None;
            /// @brief Flush requested for the destination after copying.
            IO::Types::FlushMode flushMode = IO::Types::FlushMode::None;
        };

        struct LockResult;
    } // namespace Types

    /// @brief Move-only, bounded-memory cursor over direct directory children.
    /// @details The cursor owns backend enumeration state and applies the same filters, symlink policy, native ordering,
    /// and maximum-entry contract as listDirectory(). Retained memory is independent of the number of sibling entries.
    /// A cursor object is not safe for concurrent calls.
    class DirectoryCursor final
    {
    public:
        /// @brief Creates a closed cursor.
        DirectoryCursor() noexcept;
        /// @brief Directory cursors are not copy-constructible.
        DirectoryCursor(const DirectoryCursor &) = delete;
        /// @brief Directory cursors are not copy-assignable.
        DirectoryCursor &operator=(const DirectoryCursor &) = delete;
        /// @brief Move-constructs a cursor and leaves the source closed.
        DirectoryCursor(DirectoryCursor &&other) noexcept;
        /// @brief Move-assigns a cursor, closing any enumeration previously owned by this object.
        DirectoryCursor &operator=(DirectoryCursor &&other) noexcept;
        /// @brief Closes an active enumeration without throwing.
        ~DirectoryCursor() noexcept;

        /// @brief Opens a direct-child enumeration. Failure leaves this cursor closed.
        /// @param path Directory path to enumerate.
        /// @param options Filtering, symlink, hidden-entry, and entry-limit behavior.
        /// @return Success, AlreadyOpen, or a validation/open failure status.
        [[nodiscard]] IO::Types::Status open(const Types::Path &path, const Types::ListDirectoryOptions &options = {}) noexcept;
        /// @brief Returns whether this object owns an active enumeration.
        [[nodiscard]] bool isOpen() const noexcept;
        /// @brief Returns the next accepted child or successful exhaustion.
        /// @return One entry, successful exhaustion, NotOpen, SizeLimitExceeded, or an enumeration failure.
        [[nodiscard]] Types::DirectoryCursorNextResult next() noexcept;
        /// @brief Closes the enumeration. Repeated close calls succeed.
        [[nodiscard]] IO::Types::Status close() noexcept;

    private:
        std::unique_ptr<Detail::DirectoryCursorState> state_;
        Types::ListDirectoryOptions options_{};
        std::uint64_t emittedEntries_ = 0;
        bool limitReached_ = false;
    };

    /// @brief RAII owner for a whole-file lock.
    /// @details FileLock is move-constructible and non-copyable. Move assignment is deleted because replacing an active lock could hide unlock
    /// failure. An acquired lock owns independent native state and can remain active after the originating handle object is destroyed.
    class FileLock final
    {
    public:
        /// @brief Creates an inactive lock owner.
        FileLock() noexcept;
        /// @brief File locks are not copy-constructible.
        FileLock(const FileLock &) = delete;
        /// @brief File locks are not copy-assignable.
        FileLock &operator=(const FileLock &) = delete;
        /// @brief Move-constructs a lock and transfers unlock responsibility.
        /// @param other Lock owner to empty.
        FileLock(FileLock &&other) noexcept;
        /// @brief Move assignment is disabled to avoid hiding an unlock failure.
        FileLock &operator=(FileLock &&other) = delete;
        /// @brief Releases an active lock on a best-effort basis without throwing.
        /// @note Use unlock() when release failure must be observed.
        ~FileLock() noexcept;

        /// @brief Returns whether this object owns an active lock.
        /// @return True until the lock is moved from or successfully unlocked.
        [[nodiscard]] bool active() const noexcept;
        /// @brief Returns the active lock mode.
        /// @return Shared or exclusive mode selected during acquisition.
        /// @note Meaningful only while active() is true.
        [[nodiscard]] Types::FileLockMode mode() const noexcept;
        /// @brief Releases the lock. A failed unlock remains active and may be retried.
        /// @return Success, UnlockFailed, or a more specific backend status.
        [[nodiscard]] IO::Types::Status unlock() noexcept;

    private:
        friend class File;
        friend class FileReader;
        friend class FileWriter;

        explicit FileLock(std::unique_ptr<Detail::FileLockState> state, Types::FileLockMode mode) noexcept;

        std::unique_ptr<Detail::FileLockState> state_;
        Types::FileLockMode mode_ = Types::FileLockMode::Shared;
    };

    /// @brief Read-only file-backed IO reader.
    /// @details Move construction transfers ownership. Move assignment is deleted because replacing an open handle could hide close failure.
    class FileReader final : public IO::Reader
    {
    public:
        /// @brief Creates a closed reader.
        FileReader() noexcept;
        /// @brief File readers are not copy-constructible.
        FileReader(const FileReader &) = delete;
        /// @brief File readers are not copy-assignable.
        FileReader &operator=(const FileReader &) = delete;
        /// @brief Move-constructs a reader and transfers its open handle.
        /// @param other Reader to leave closed.
        FileReader(FileReader &&other) noexcept;
        /// @brief Move assignment is disabled to avoid hiding close failure.
        FileReader &operator=(FileReader &&other) = delete;
        /// @brief Closes an open file on a best-effort basis without throwing.
        ~FileReader() noexcept override;

        /// @brief Opens a path. Failure leaves this reader closed.
        /// @param path File path to open.
        /// @param options Sharing and symlink-resolution behavior.
        /// @return Success, AlreadyOpen when already open, or an open failure status.
        [[nodiscard]] IO::Types::Status open(const Types::Path &path, const Types::FileReaderOpenOptions &options = {}) noexcept;
        /// @brief Returns whether a file is currently open.
        /// @return True after a successful open() and before close().
        [[nodiscard]] bool isOpen() const noexcept override;
        /// @brief Returns whether seek operations are currently available.
        /// @return True while a normal file handle is open.
        [[nodiscard]] bool canSeek() const noexcept override;
        /// @brief Reads bytes from the current file position.
        /// @param destination Caller-owned call-scoped memory; the reader does not retain it.
        /// @return Read status, copied byte count, and end-of-stream state. A successful final read may contain bytes and set endOfStream.
        /// @note An empty destination performs no transfer and queries the current end-of-stream state.
        [[nodiscard]] IO::Types::ReadResult read(std::span<std::byte> destination) noexcept override;
        /// @brief Closes the file. Repeated close calls succeed.
        /// @return ResourceBusy while a lock acquired from this reader remains active, or a close failure status.
        /// @note Failure leaves the reader open so close() can be retried.
        [[nodiscard]] IO::Types::Status close() noexcept override;
        /// @brief Returns the current byte position.
        /// @return Current position, or NotOpen while closed.
        [[nodiscard]] IO::Types::PositionResult position() const noexcept override;
        /// @brief Returns the current file size.
        /// @return File size, or NotOpen while closed.
        [[nodiscard]] IO::Types::SizeResult size() const noexcept override;
        /// @brief Moves the current file position.
        /// @param offset Signed offset relative to origin.
        /// @param origin Origin used to interpret offset.
        /// @return Success, NotOpen, InvalidArgument, or a seek failure status.
        [[nodiscard]] IO::Types::Status seek(std::int64_t offset, IO::Types::SeekOrigin origin) noexcept override;
        /// @brief Attempts to acquire a non-blocking shared whole-file lock.
        /// @return Lock status, acquisition outcome, and active lock owner when acquired.
        [[nodiscard]] Types::LockResult tryLockShared() noexcept;

    private:
        std::unique_ptr<Detail::FileState> state_;
    };

    /// @brief Write-only file-backed IO writer.
    /// @details Move construction transfers ownership. Move assignment is deleted because replacing an open handle could hide flush or close failure.
    class FileWriter final : public IO::Writer
    {
    public:
        /// @brief Creates a closed writer.
        FileWriter() noexcept;
        /// @brief File writers are not copy-constructible.
        FileWriter(const FileWriter &) = delete;
        /// @brief File writers are not copy-assignable.
        FileWriter &operator=(const FileWriter &) = delete;
        /// @brief Move-constructs a writer and transfers its open handle.
        /// @param other Writer to leave closed.
        FileWriter(FileWriter &&other) noexcept;
        /// @brief Move assignment is disabled to avoid hiding flush or close failure.
        FileWriter &operator=(FileWriter &&other) = delete;
        /// @brief Flushes and closes an open file on a best-effort basis without throwing.
        ~FileWriter() noexcept override;

        /// @brief Opens a path. Failure leaves this writer closed.
        /// @param path File path to open.
        /// @param options Creation, sharing, symlink, parent, and close-flush behavior.
        /// @return Success, AlreadyOpen when already open, or an open failure status.
        /// @note Append modes are non-seekable and each write targets the then-current end of file.
        [[nodiscard]] IO::Types::Status open(const Types::Path &path, const Types::FileWriterOpenOptions &options = {}) noexcept;
        /// @brief Returns whether a file is currently open.
        /// @return True after a successful open() and before close().
        [[nodiscard]] bool isOpen() const noexcept override;
        /// @brief Returns whether seek operations are currently available.
        /// @return False for append modes and while closed; true for other open modes.
        [[nodiscard]] bool canSeek() const noexcept override;
        /// @brief Writes bytes at the current position or current end in append mode.
        /// @param bytes Caller-owned call-scoped bytes; the writer does not retain them.
        /// @return Write status and accepted byte count. Success can report fewer bytes than requested.
        /// @note Empty input succeeds without transferring bytes.
        [[nodiscard]] IO::Types::WriteResult write(std::span<const std::byte> bytes) noexcept override;
        /// @brief Flushes written data according to the requested strength.
        /// @param mode Requested flush strength. None validates state but requests no physical flush.
        /// @return Success, NotOpen, InvalidArgument, or a flush failure status.
        [[nodiscard]] IO::Types::Status flush(IO::Types::FlushMode mode = IO::Types::FlushMode::Data) noexcept override;
        /// @brief Applies configured flush-on-close and closes the file. Repeated close calls succeed.
        /// @return ResourceBusy while a lock acquired from this writer remains active, or a flush/close failure status.
        /// @note Failure leaves the writer open so close() can be retried.
        [[nodiscard]] IO::Types::Status close() noexcept override;
        /// @brief Returns the current byte position when meaningful.
        /// @return Current position, NotOpen, or NotSeekable for append mode.
        [[nodiscard]] IO::Types::PositionResult position() const noexcept override;
        /// @brief Returns the current file size.
        /// @return File size, or NotOpen while closed.
        [[nodiscard]] IO::Types::SizeResult size() const noexcept;
        /// @brief Moves the current file position outside append mode.
        /// @param offset Signed offset relative to origin.
        /// @param origin Origin used to interpret offset.
        /// @return Success, NotOpen, NotSeekable, InvalidArgument, or a seek failure status.
        [[nodiscard]] IO::Types::Status seek(std::int64_t offset, IO::Types::SeekOrigin origin) noexcept override;
        /// @brief Attempts to acquire a non-blocking exclusive whole-file lock.
        /// @return Lock status, acquisition outcome, and active lock owner when acquired.
        [[nodiscard]] Types::LockResult tryLockExclusive() noexcept;

    private:
        std::unique_ptr<Detail::FileState> state_;
    };

    /// @brief Read/write file-backed IO stream for modification workflows.
    /// @details Move construction transfers ownership. Move assignment is deleted because replacing an open handle could hide flush or close failure.
    class File final : public IO::Reader, public IO::Writer
    {
    public:
        /// @brief Creates a closed read/write file.
        File() noexcept;
        /// @brief Files are not copy-constructible.
        File(const File &) = delete;
        /// @brief Files are not copy-assignable.
        File &operator=(const File &) = delete;
        /// @brief Move-constructs a file and transfers its open handle.
        /// @param other File to leave closed.
        File(File &&other) noexcept;
        /// @brief Move assignment is disabled to avoid hiding flush or close failure.
        File &operator=(File &&other) = delete;
        /// @brief Flushes and closes an open file on a best-effort basis without throwing.
        ~File() noexcept override;

        /// @brief Opens a path. Failure leaves this object closed.
        /// @param path File path to open.
        /// @param options Access, creation, sharing, position, symlink, parent, and close-flush behavior.
        /// @return Success, AlreadyOpen when already open, or an open failure status.
        /// @note FileInitialPosition::End sets one initial position; it does not provide append semantics.
        /// @note Modes that create or truncate require Write or ReadWrite access.
        /// @note A non-None flushOnClose requires Write or ReadWrite access.
        [[nodiscard]] IO::Types::Status open(const Types::Path &path, const Types::FileOpenOptions &options = {}) noexcept;
        /// @brief Returns whether a file is currently open.
        /// @return True after a successful open() and before close().
        [[nodiscard]] bool isOpen() const noexcept override;
        /// @brief Returns whether seek operations are currently available.
        /// @return True while a normal file handle is open.
        [[nodiscard]] bool canSeek() const noexcept override;
        /// @brief Returns access selected by the successful open call.
        /// @return Access mode selected by open().
        /// @note Meaningful only while isOpen() is true.
        [[nodiscard]] Types::FileAccess access() const noexcept;
        /// @brief Reads bytes from the current file position.
        /// @param destination Caller-owned call-scoped memory; the reader does not retain it.
        /// @return Read status, copied byte count, and end-of-stream state. A successful final read may contain bytes and set endOfStream.
        /// @note An empty destination performs no transfer and queries the current end-of-stream state.
        [[nodiscard]] IO::Types::ReadResult read(std::span<std::byte> destination) noexcept override;
        /// @brief Writes bytes at the current file position.
        /// @param bytes Caller-owned bytes to write.
        /// @return Write status and accepted byte count.
        [[nodiscard]] IO::Types::WriteResult write(std::span<const std::byte> bytes) noexcept override;
        /// @brief Flushes written data according to the requested strength.
        /// @param mode Requested flush strength. None validates state but requests no physical flush.
        /// @return Success, NotOpen, InvalidArgument, or a flush failure status.
        [[nodiscard]] IO::Types::Status flush(IO::Types::FlushMode mode = IO::Types::FlushMode::Data) noexcept override;
        /// @brief Applies configured flush-on-close and closes the file. Repeated close calls succeed.
        /// @return ResourceBusy while a lock acquired from this file remains active, or a flush/close failure status.
        /// @note Failure leaves the file open so close() can be retried.
        [[nodiscard]] IO::Types::Status close() noexcept override;
        /// @brief Returns the current byte position.
        /// @return Current position, or NotOpen while closed.
        [[nodiscard]] IO::Types::PositionResult position() const noexcept override;
        /// @brief Returns the current file size.
        /// @return File size, or NotOpen while closed.
        [[nodiscard]] IO::Types::SizeResult size() const noexcept override;
        /// @brief Moves the current file position.
        /// @param offset Signed offset relative to origin.
        /// @param origin Origin used to interpret offset.
        /// @return Success, NotOpen, InvalidArgument, or a seek failure status.
        [[nodiscard]] IO::Types::Status seek(std::int64_t offset, IO::Types::SeekOrigin origin) noexcept override;
        /// @brief Resizes the open file.
        /// @param sizeBytes Requested file size in bytes.
        /// @return Success, NotOpen, PermissionDenied for read-only access, SizeLimitExceeded, or a resize failure status.
        /// @note On success, the previous position is restored when it still fits; otherwise a shrink leaves the position at the new end.
        [[nodiscard]] IO::Types::Status resize(std::uint64_t sizeBytes) noexcept;
        /// @brief Attempts to acquire a non-blocking shared whole-file lock.
        /// @return Lock status, acquisition outcome, and active lock owner when acquired.
        [[nodiscard]] Types::LockResult tryLockShared() noexcept;
        /// @brief Attempts to acquire a non-blocking exclusive whole-file lock.
        /// @return Lock status, acquisition outcome, and active lock owner when acquired.
        [[nodiscard]] Types::LockResult tryLockExclusive() noexcept;

    private:
        std::unique_ptr<Detail::FileState> state_;
    };

    namespace Types
    {
        /// @brief Result returned by a non-blocking whole-file lock attempt.
        /// @details WouldBlock is a successful expected outcome with an inactive lock.
        struct LockResult
        {
            /// @brief Operation status. WouldBlock is reported through outcome, not as an error.
            IO::Types::Status status;
            /// @brief Whether the lock was acquired or could not be acquired immediately.
            LockOutcome outcome = LockOutcome::WouldBlock;
            /// @brief Active lock owner when outcome is Acquired; inactive otherwise.
            FileLock lock;
        };
    } // namespace Types

    /// @brief Reads an entire file as bytes.
    /// @param path File path to read.
    /// @param options Open behavior, hard byte limit, and transfer buffer size.
    /// @return Collected bytes and final status. Partial data may be present after transfer or close failure.
    /// @note SizeLimitExceeded is a failed hard-limit result, not successful truncation.
    [[nodiscard]] IO::Types::ReadAllBytesResult readAllBytes(const Types::Path &path, const Types::ReadFileOptions &options = {}) noexcept;

    /// @brief Reads an entire file as text bytes without validation, BOM handling, or encoding conversion.
    /// @param path File path to read.
    /// @param options Open behavior, hard byte limit, and transfer buffer size.
    /// @return Collected text bytes and final status. Partial text may be present after transfer or close failure.
    [[nodiscard]] IO::Types::ReadAllTextResult readAllText(const Types::Path &path, const Types::ReadFileOptions &options = {}) noexcept;

    /// @brief Writes exact file contents through a non-atomic writer and reports accepted payload bytes.
    /// @details Empty input still performs the requested create/truncate, flush, and close sequence. A flush or close failure preserves the payload
    /// byte count accepted before that failure.
    /// @param path Destination file path.
    /// @param bytes Exact payload bytes.
    /// @param options Creation, sharing, symlink, parent, and flush behavior.
    /// @return Final status and total payload bytes accepted.
    [[nodiscard]] IO::Types::WriteResult writeAllBytes(
        const Types::Path &path,
        std::span<const std::byte> bytes,
        const Types::WriteFileOptions &options = {}) noexcept;

    /// @brief Writes exact vector contents and reports accepted payload bytes.
    /// @tparam Allocator Byte-vector allocator type.
    /// @param path Destination file path.
    /// @param bytes Exact payload bytes.
    /// @param options Creation, sharing, symlink, parent, and flush behavior.
    /// @return Final status and total payload bytes accepted.
    template <typename Allocator>
    [[nodiscard]] IO::Types::WriteResult writeAllBytes(
        const Types::Path &path,
        const std::vector<std::byte, Allocator> &bytes,
        const Types::WriteFileOptions &options = {}) noexcept
    {
        return writeAllBytes(path, std::span<const std::byte>(bytes.data(), bytes.size()), options);
    }

    /// @brief Writes exact text bytes without validation, BOM handling, or encoding conversion.
    /// @details Empty input still performs the requested create/truncate, flush, and close sequence. A flush or close failure preserves the payload
    /// byte count accepted before that failure.
    /// @param path Destination file path.
    /// @param utf8Text Exact text bytes.
    /// @param options Creation, sharing, symlink, parent, and flush behavior.
    /// @return Final status and total payload bytes accepted.
    [[nodiscard]] IO::Types::WriteResult writeAllText(
        const Types::Path &path,
        std::string_view utf8Text,
        const Types::WriteFileOptions &options = {}) noexcept;

    /// @brief Appends bytes through an append-mode handle and reports accepted payload bytes.
    /// @details A flush or close failure preserves the payload byte count accepted before that failure.
    /// @param path Destination file path.
    /// @param bytes Payload bytes to append.
    /// @param options Creation, sharing, symlink, parent, and flush behavior.
    /// @return Final status and total payload bytes accepted.
    [[nodiscard]] IO::Types::WriteResult appendBytes(
        const Types::Path &path,
        std::span<const std::byte> bytes,
        const Types::AppendFileOptions &options = {}) noexcept;

    /// @brief Appends vector contents and reports accepted payload bytes.
    /// @tparam Allocator Byte-vector allocator type.
    /// @param path Destination file path.
    /// @param bytes Payload bytes to append.
    /// @param options Creation, sharing, symlink, parent, and flush behavior.
    /// @return Final status and total payload bytes accepted.
    template <typename Allocator>
    [[nodiscard]] IO::Types::WriteResult appendBytes(
        const Types::Path &path,
        const std::vector<std::byte, Allocator> &bytes,
        const Types::AppendFileOptions &options = {}) noexcept
    {
        return appendBytes(path, std::span<const std::byte>(bytes.data(), bytes.size()), options);
    }

    /// @brief Appends UTF-8 bytes without adding a line ending and reports accepted payload bytes.
    /// @details A flush or close failure preserves the payload byte count accepted before that failure.
    /// @param path Destination file path.
    /// @param utf8Text Text bytes to append.
    /// @param options Creation, sharing, symlink, parent, and flush behavior.
    /// @return Final status and total payload bytes accepted.
    [[nodiscard]] IO::Types::WriteResult appendText(
        const Types::Path &path,
        std::string_view utf8Text,
        const Types::AppendFileOptions &options = {}) noexcept;

    /// @brief Atomically replaces exact file contents through a same-directory temporary file.
    /// @details No non-atomic fallback is used. Before commit, failure leaves an existing destination unchanged.
    /// Temporary files use restrictive access, and security metadata failures are not silently ignored.
    /// @param path Destination file path.
    /// @param bytes Exact replacement payload.
    /// @param options Parent creation, replacement, symlink, flush, and temporary-name behavior.
    /// @return Success or the validation, temporary-file, write, flush, or replacement failure.
    [[nodiscard]] IO::Types::Status writeAllBytesAtomic(
        const Types::Path &path,
        std::span<const std::byte> bytes,
        const Types::AtomicWriteOptions &options = {}) noexcept;

    /// @brief Atomically replaces exact file contents from vector storage.
    /// @tparam Allocator Byte-vector allocator type.
    /// @param path Destination file path.
    /// @param bytes Exact replacement payload.
    /// @param options Parent creation, replacement, symlink, flush, and temporary-name behavior.
    /// @return Success or the validation, temporary-file, write, flush, or replacement failure.
    template <typename Allocator>
    [[nodiscard]] IO::Types::Status writeAllBytesAtomic(
        const Types::Path &path,
        const std::vector<std::byte, Allocator> &bytes,
        const Types::AtomicWriteOptions &options = {}) noexcept
    {
        return writeAllBytesAtomic(path, std::span<const std::byte>(bytes.data(), bytes.size()), options);
    }

    /// @brief Atomically replaces exact UTF-8 byte contents.
    /// @param path Destination file path.
    /// @param utf8Text Exact replacement text bytes.
    /// @param options Parent creation, replacement, symlink, flush, and temporary-name behavior.
    /// @return Success or the validation, temporary-file, write, flush, or replacement failure.
    [[nodiscard]] IO::Types::Status writeAllTextAtomic(
        const Types::Path &path,
        std::string_view utf8Text,
        const Types::AtomicWriteOptions &options = {}) noexcept;

    /// @brief Resizes an existing regular file.
    /// @param path File path to resize.
    /// @param sizeBytes Requested file size in bytes.
    /// @param options Symlink traversal behavior.
    /// @return Success or a validation, lookup, permission, or resize failure status.
    [[nodiscard]] IO::Types::Status resizeFile(const Types::Path &path, std::uint64_t sizeBytes, const Types::MutationOptions &options = {}) noexcept;

    /// @brief Truncates an existing regular file to zero bytes.
    /// @param path File path to truncate.
    /// @param options Symlink traversal behavior.
    /// @return Success or a validation, lookup, permission, or resize failure status.
    [[nodiscard]] IO::Types::Status truncateFile(const Types::Path &path, const Types::MutationOptions &options = {}) noexcept;

    /// @brief Creates one directory level.
    /// @param path Directory path to create.
    /// @param options Existing-directory and symlink traversal behavior.
    /// @return Success or a validation, conflict, permission, or directory-creation failure status.
    [[nodiscard]] IO::Types::Status createDirectory(const Types::Path &path, const Types::CreateDirectoryOptions &options = {}) noexcept;

    /// @brief Creates a directory and any missing parent directories.
    /// @param path Directory path to create.
    /// @param options Existing-directory and symlink traversal behavior.
    /// @return Success or a validation, conflict, permission, or directory-creation failure status.
    [[nodiscard]] IO::Types::Status createDirectories(const Types::Path &path, const Types::CreateDirectoryOptions &options = {}) noexcept;

    /// @brief Lists direct children of a directory in backend/native order.
    /// @param path Directory path to enumerate.
    /// @param options Filtering, symlink, hidden-entry, and entry-limit behavior.
    /// @return Collected child entries and final status.
    [[nodiscard]] Types::ListDirectoryResult listDirectory(const Types::Path &path, const Types::ListDirectoryOptions &options = {}) noexcept;

    /// @brief Tests whether a filesystem entry exists.
    /// @param path Path to query.
    /// @param options Symlink traversal behavior.
    /// @return Successful true or false; missing entries produce successful false.
    [[nodiscard]] Types::BoolResult exists(const Types::Path &path, const Types::QueryOptions &options = {}) noexcept;

    /// @brief Returns portable metadata for an existing filesystem entry.
    /// @param path Path to query.
    /// @param options Symlink traversal behavior.
    /// @return Entry metadata, or NotFound when the entry is missing.
    [[nodiscard]] Types::EntryInfoResult getEntryInfo(const Types::Path &path, const Types::QueryOptions &options = {}) noexcept;

    /// @brief Tests whether a path exists and is a regular file.
    /// @param path Path to query.
    /// @param options Symlink traversal behavior.
    /// @return Successful true or false; missing entries produce successful false.
    [[nodiscard]] Types::BoolResult isRegularFile(const Types::Path &path, const Types::QueryOptions &options = {}) noexcept;

    /// @brief Tests whether a path exists and is a directory.
    /// @param path Path to query.
    /// @param options Symlink traversal behavior.
    /// @return Successful true or false; missing entries produce successful false.
    [[nodiscard]] Types::BoolResult isDirectory(const Types::Path &path, const Types::QueryOptions &options = {}) noexcept;

    /// @brief Tests whether a path exists and is a symbolic link or equivalent link-like entry.
    /// @param path Path to query.
    /// @param options Symlink traversal behavior.
    /// @return Successful true or false; missing entries produce successful false.
    [[nodiscard]] Types::BoolResult isSymlink(const Types::Path &path, const Types::QueryOptions &options = {}) noexcept;

    /// @brief Returns the size of an existing regular file.
    /// @param path File path to query.
    /// @param options Symlink traversal behavior.
    /// @return File size, NotFound when missing, or InvalidArgument when the resolved entry is not a regular file with portable size.
    [[nodiscard]] IO::Types::SizeResult getFileSize(const Types::Path &path, const Types::QueryOptions &options = {}) noexcept;

    /// @brief Returns the last-write time of an existing filesystem entry.
    /// @param path Path to query.
    /// @param options Symlink traversal behavior.
    /// @return Last-write time, or NotFound when the entry is missing.
    [[nodiscard]] Types::LastWriteTimeResult getLastWriteTime(const Types::Path &path, const Types::QueryOptions &options = {}) noexcept;

    /// @brief Returns the portable basic read-only state of an existing entry.
    /// @param path Path to query.
    /// @param options Symlink traversal behavior.
    /// @return Successful true or false, or NotFound when the entry is missing.
    [[nodiscard]] Types::BoolResult isReadOnly(const Types::Path &path, const Types::QueryOptions &options = {}) noexcept;

    /// @brief Changes the portable read-only state of an existing entry.
    /// @param path Path to update.
    /// @param readOnly True to request read-only state; false to request writable state.
    /// @param options Symlink traversal behavior.
    /// @return Success or a validation, lookup, permission, or metadata failure status.
    [[nodiscard]] IO::Types::Status setReadOnly(const Types::Path &path, bool readOnly, const Types::QueryOptions &options = {}) noexcept;

    /// @brief Copies one regular file.
    /// @param from Source file path.
    /// @param to Destination file path.
    /// @param options Replacement, symlink, parent, metadata, and flush behavior.
    /// @return Success or a validation, lookup, permission, copy, metadata, or flush failure status.
    [[nodiscard]] IO::Types::Status copyFile(const Types::Path &from, const Types::Path &to, const Types::CopyFileOptions &options = {}) noexcept;

    /// @brief Moves or renames one filesystem entry.
    /// @param from Source path.
    /// @param to Destination path.
    /// @param options Replacement, symlink, and parent creation behavior.
    /// @return Success or a validation, lookup, permission, conflict, or move failure status.
    /// @note Cross-volume moves return MoveFailed; no copy-and-delete fallback is performed.
    /// @note Native rename success is the operation's linearization point; later namespace changes do not change the returned success.
    [[nodiscard]] IO::Types::Status movePath(const Types::Path &from, const Types::Path &to, const Types::MoveOptions &options = {}) noexcept;

    /// @brief Removes one regular file or symlink-to-file entry.
    /// @param path Path to remove.
    /// @param options Missing-target and symlink behavior.
    /// @return Success or a validation, lookup, type, permission, or removal failure status.
    [[nodiscard]] IO::Types::Status removeFile(const Types::Path &path, const Types::RemoveOptions &options = {}) noexcept;

    /// @brief Removes one empty directory.
    /// @param path Directory path to remove.
    /// @param options Missing-target and symlink behavior.
    /// @return Success, DirectoryNotEmpty, or another validation, lookup, type, permission, or removal failure status.
    [[nodiscard]] IO::Types::Status removeEmptyDirectory(const Types::Path &path, const Types::RemoveOptions &options = {}) noexcept;

    /// @brief Recursively removes a directory tree without following discovered symlinked directories.
    /// @param path Root directory path to remove.
    /// @param options Missing-target, initial symlink, and entry-limit behavior.
    /// @return Final status and number of entries removed before completion or failure.
    [[nodiscard]] Types::RemoveDirectoryTreeResult removeDirectoryTree(
        const Types::Path &path,
        const Types::RemoveDirectoryTreeOptions &options = {}) noexcept;

    /// @brief Returns the process current working directory.
    /// @return Current working directory path or a query failure status.
    [[nodiscard]] Types::PathResult getCurrentDirectory() noexcept;

    /// @brief Sets the process current working directory.
    /// @param path Existing directory to make current.
    /// @return Success or a validation, lookup, type, permission, or native failure status.
    [[nodiscard]] IO::Types::Status setCurrentDirectory(const Types::Path &path) noexcept;

    /// @brief Returns the parent component of a path.
    /// @param path Path to inspect.
    /// @return Parent path or a conversion/allocation failure status.
    [[nodiscard]] Types::PathResult parentPath(const Types::Path &path) noexcept;

    /// @brief Returns the filename component of a path.
    /// @param path Path to inspect.
    /// @return Filename path or a conversion/allocation failure status.
    [[nodiscard]] Types::PathResult filename(const Types::Path &path) noexcept;

    /// @brief Returns the stem component of a path filename.
    /// @param path Path to inspect.
    /// @return Stem path or a conversion/allocation failure status.
    [[nodiscard]] Types::PathResult stem(const Types::Path &path) noexcept;

    /// @brief Returns the extension component of a path filename.
    /// @param path Path to inspect.
    /// @return Extension path or a conversion/allocation failure status.
    [[nodiscard]] Types::PathResult extension(const Types::Path &path) noexcept;

    /// @brief Returns a copy of a path with its extension replaced.
    /// @param path Source path.
    /// @param newExtension Replacement extension, with or without a leading period.
    /// @return Updated path or a conversion/allocation failure status.
    [[nodiscard]] Types::PathResult replaceExtension(const Types::Path &path, const Types::Path &newExtension) noexcept;

    /// @brief Joins two path parts using std::filesystem::path component rules.
    /// @param left Base path.
    /// @param right Path component to append. A rooted right-hand path may replace part or all of left.
    /// @return Joined path or a conversion/allocation failure status.
    /// @note This is lexical and does not access, normalize, or canonicalize the filesystem.
    [[nodiscard]] Types::PathResult joinPath(const Types::Path &left, const Types::Path &right) noexcept;

    /// @brief Tests whether a path is absolute according to platform path rules.
    /// @param path Path to inspect.
    /// @return Successful boolean result or a conversion/allocation failure status.
    [[nodiscard]] Types::BoolResult isAbsolutePath(const Types::Path &path) noexcept;

    /// @brief Tests whether a path is relative according to platform path rules.
    /// @param path Path to inspect.
    /// @return Successful boolean result or a conversion/allocation failure status.
    [[nodiscard]] Types::BoolResult isRelativePath(const Types::Path &path) noexcept;

    /// @brief Converts a path to an absolute path using the process current directory when needed.
    /// @param path Non-empty path to resolve.
    /// @return Absolute path or a query/conversion/allocation failure status.
    /// @note This does not require the target to exist and does not promise canonical or normalized spelling.
    [[nodiscard]] Types::PathResult absolutePath(const Types::Path &path) noexcept;

    /// @brief Resolves a canonical path whose complete target must exist.
    /// @param path Non-empty path to resolve.
    /// @return Canonical path or a lookup, permission, conversion, or allocation failure status.
    /// @note Uses ordinary std::filesystem symlink resolution and does not apply SymlinkPolicy.
    [[nodiscard]] Types::PathResult canonicalPath(const Types::Path &path) noexcept;

    /// @brief Resolves a best-effort canonical path that permits missing trailing components.
    /// @param path Non-empty path to resolve.
    /// @return Weakly canonical path or a permission, conversion, or allocation failure status.
    /// @note Uses ordinary std::filesystem symlink resolution and does not apply SymlinkPolicy.
    [[nodiscard]] Types::PathResult weaklyCanonicalPath(const Types::Path &path) noexcept;

    /// @brief Returns the platform temporary-directory path.
    /// @return Temporary-directory path or a query/conversion/allocation failure status.
    [[nodiscard]] Types::PathResult getTemporaryDirectoryPath() noexcept;

    /// @brief Converts UTF-8 text to the platform-native Path representation.
    /// @param utf8Path UTF-8 path text.
    /// @return Converted path or EncodingFailed/OutOfMemory.
    /// @note Conversion does not make the path absolute, canonical, or normalized.
    [[nodiscard]] Types::PathResult pathFromUtf8(std::string_view utf8Path) noexcept;

    /// @brief Converts a Path's stored spelling to UTF-8 text.
    /// @param path Path to convert.
    /// @return UTF-8 path text or EncodingFailed/OutOfMemory.
    /// @note The result does not promise normalized or platform-independent separators.
    [[nodiscard]] Types::Utf8PathResult pathToUtf8(const Types::Path &path) noexcept;
} // namespace GameWIP::FileSystem
