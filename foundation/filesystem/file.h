/// @file file.h
/// @brief Public file streams, locking, and whole-file operations for GameWIP FileSystem.

#pragma once

#include "filesystem/entry.h"
#include "io/transfer.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace GameWIP::FileSystem
{
    /// @brief Default prefix used for same-directory temporary files created by atomic writes.
    /// @note Callers may replace the prefix through Types::File::AtomicWriteOptions; it is UTF-8 filename text, not a path.
    inline constexpr std::string_view kAtomicTemporaryNamePrefix = ".gamewip_tmp_";

    namespace Detail
    {
        struct FileState;
        struct FileLockState;
    } // namespace Detail

    class File;
    class FileReader;
    class FileWriter;
    class FileLock;

    namespace Types
    {
        /// @brief File-handle and whole-file policies.
        namespace File
        {
            /// @brief Access requested for a read/write File handle.
            enum class Access
            {
                /// @brief Open for reading only.
                Read,
                /// @brief Open for writing only.
                Write,
                /// @brief Open for both reading and writing.
                ReadWrite
            };

            /// @brief Bitmask controlling access allowed to other opens while a handle remains open.
            enum class Share : std::uint8_t
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
            [[nodiscard]] constexpr Share operator|(Share left, Share right) noexcept
            {
                return static_cast<Share>(static_cast<std::uint8_t>(left) | static_cast<std::uint8_t>(right));
            }

            /// @brief Intersects two file-sharing flags.
            /// @param left First sharing mask.
            /// @param right Second sharing mask.
            /// @return Bitwise intersection of left and right.
            [[nodiscard]] constexpr Share operator&(Share left, Share right) noexcept
            {
                return static_cast<Share>(static_cast<std::uint8_t>(left) & static_cast<std::uint8_t>(right));
            }

            /// @brief Adds sharing flags to an existing mask.
            /// @param left Sharing mask to update.
            /// @param right Sharing flags to add.
            /// @return Reference to the updated left mask.
            constexpr Share &operator|=(Share &left, Share right) noexcept
            {
                left = left | right;
                return left;
            }

            /// @brief Retains only sharing flags present in both masks.
            /// @param left Sharing mask to update.
            /// @param right Sharing flags to retain.
            /// @return Reference to the updated left mask.
            constexpr Share &operator&=(Share &left, Share right) noexcept
            {
                left = left & right;
                return left;
            }

            /// @brief Creation and truncation behavior for File::open().
            enum class OpenMode
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
            enum class InitialPosition
            {
                /// @brief Position the handle at byte zero.
                Beginning,
                /// @brief Position the handle at the current end of the file.
                End
            };

            /// @brief Creation, truncation, and append behavior for FileWriter::open().
            enum class WriterMode
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
            enum class WriteMode
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
        } // namespace File

        /// @brief Whole-file lock policies and results.
        namespace Lock
        {
            /// @brief Active whole-file lock mode.
            enum class Mode
            {
                /// @brief Shared lock intended to coexist with other shared locks.
                Shared,
                /// @brief Exclusive lock intended to exclude other compatible locks.
                Exclusive
            };

            /// @brief Outcome of a non-blocking lock attempt.
            enum class Outcome
            {
                /// @brief The requested lock was acquired.
                Acquired,
                /// @brief Another lock prevented immediate acquisition.
                WouldBlock
            };

            struct Result;
        } // namespace Lock

        namespace File
        {
            /// @brief Metadata copied with file contents.
            enum class CopyMetadataMode
            {
                /// @brief Copy file contents only.
                None,
                /// @brief Copy portable basic metadata such as last-write time and read-only state where supported.
                Basic
            };

            /// @brief Options used by File::open().
            struct OpenOptions
            {
                /// @brief Requested read/write access.
                Access access = Access::ReadWrite;
                /// @brief Creation and truncation behavior.
                OpenMode mode = OpenMode::OpenExisting;
                /// @brief Initial handle position after a successful open.
                InitialPosition initialPosition = InitialPosition::Beginning;
                /// @brief Access allowed to other opens while this handle remains open.
                Share share = Share::All;
                /// @brief Symlink traversal policy used while resolving the path.
                SymlinkPolicy symlinkPolicy = SymlinkPolicy::DoNotFollow;
                /// @brief Create missing parent directories before opening.
                bool createParentDirectories = false;
                /// @brief Flush requested by explicit close() before releasing the handle.
                IO::Types::FlushMode flushOnClose = IO::Types::FlushMode::None;
            };

            /// @brief Options used by FileReader::open().
            struct ReaderOpenOptions
            {
                /// @brief Access allowed to other opens while this reader remains open.
                Share share = Share::All;
                /// @brief Symlink traversal policy used while resolving the path.
                SymlinkPolicy symlinkPolicy = SymlinkPolicy::DoNotFollow;
            };

            /// @brief Options used by FileWriter::open().
            struct WriterOpenOptions
            {
                /// @brief Creation, truncation, or append behavior.
                WriterMode mode = WriterMode::CreateOrTruncate;
                /// @brief Access allowed to other opens while this writer remains open.
                Share share = Share::All;
                /// @brief Symlink traversal policy used while resolving the path.
                SymlinkPolicy symlinkPolicy = SymlinkPolicy::DoNotFollow;
                /// @brief Create missing parent directories before opening.
                bool createParentDirectories = false;
                /// @brief Flush requested by explicit close() before releasing the handle.
                IO::Types::FlushMode flushOnClose = IO::Types::FlushMode::None;
            };

            /// @brief Options used by whole-file read helpers.
            struct ReadOptions
            {
                /// @brief Options used to open the underlying reader.
                ReaderOpenOptions open{};
                /// @brief Maximum accepted file size, or IO::kNoByteLimit for no caller limit.
                /// @note This is a hard limit, not a successful truncation request.
                std::uint64_t maxBytes = IO::kNoByteLimit;
                /// @brief Temporary transfer buffer size. Must be greater than zero.
                /// @note The buffer is internal to the call and does not limit the final result size.
                std::size_t bufferSize = IO::kDefaultBufferSize;
            };

            /// @brief Options used by exact-content whole-file write helpers.
            struct WriteOptions
            {
                /// @brief Creation and replacement behavior.
                WriteMode mode = WriteMode::CreateOrTruncate;
                /// @brief Access allowed to other opens while the target is open.
                Share share = Share::All;
                /// @brief Symlink traversal policy used while resolving the path.
                SymlinkPolicy symlinkPolicy = SymlinkPolicy::DoNotFollow;
                /// @brief Create missing parent directories before opening.
                bool createParentDirectories = true;
                /// @brief Flush requested after payload writing and before close.
                IO::Types::FlushMode flushMode = IO::Types::FlushMode::None;
            };

            /// @brief Options used by append helpers.
            struct AppendOptions
            {
                /// @brief Whether a missing target is created or reported as NotFound.
                AppendMode mode = AppendMode::AppendOrCreate;
                /// @brief Access allowed to other opens while the target is open.
                Share share = Share::All;
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
                /// @brief UTF-8 filename prefix used for a generated temporary file in the destination directory.
                /// @note Must be valid UTF-8, non-empty, contain no path separator or embedded U+0000, and not be "." or "..".
                std::string temporaryNamePrefix{kAtomicTemporaryNamePrefix};
            };
        } // namespace File

        namespace File
        {
            /// @brief Options used by path-based file resizing and truncation.
            struct ResizeOptions
            {
                /// @brief Symlink traversal policy used to resolve the file to mutate.
                SymlinkPolicy symlinkPolicy = SymlinkPolicy::DoNotFollow;
            };
        } // namespace File

        namespace File
        {
            /// @brief Options used by copyFile().
            struct CopyOptions
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
        } // namespace File
    } // namespace Types

    // File locking
    // ------------------------------------------------------------

    /// @name File locking
    /// @{

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
        [[nodiscard]] Types::Lock::Mode mode() const noexcept;
        /// @brief Releases the lock. A failed unlock remains active and may be retried.
        /// @return Success, UnlockFailed, or a more specific backend status.
        [[nodiscard]] IO::Types::Status unlock() noexcept;

    private:
        friend class File;
        friend class FileReader;
        friend class FileWriter;

        explicit FileLock(std::unique_ptr<Detail::FileLockState> state, Types::Lock::Mode mode) noexcept;

        std::unique_ptr<Detail::FileLockState> state_;
        Types::Lock::Mode mode_ = Types::Lock::Mode::Shared;
    };

    /// @}

    // ------------------------------------------------------------
    // File streams
    // ------------------------------------------------------------

    /// @name File streams
    /// @{

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
        [[nodiscard]] IO::Types::Status open(const Types::Path &path, const Types::File::ReaderOpenOptions &options = {}) noexcept;
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
        [[nodiscard]] Types::Lock::Result tryLockShared() noexcept;

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
        [[nodiscard]] IO::Types::Status open(const Types::Path &path, const Types::File::WriterOpenOptions &options = {}) noexcept;
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
        [[nodiscard]] Types::Lock::Result tryLockExclusive() noexcept;

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
        /// @note Types::File::InitialPosition::End sets one initial position; it does not provide append semantics.
        /// @note Modes that create or truncate require Write or ReadWrite access.
        /// @note A non-None flushOnClose requires Write or ReadWrite access.
        [[nodiscard]] IO::Types::Status open(const Types::Path &path, const Types::File::OpenOptions &options = {}) noexcept;
        /// @brief Returns whether a file is currently open.
        /// @return True after a successful open() and before close().
        [[nodiscard]] bool isOpen() const noexcept override;
        /// @brief Returns whether seek operations are currently available.
        /// @return True while a normal file handle is open.
        [[nodiscard]] bool canSeek() const noexcept override;
        /// @brief Returns access selected by the successful open call.
        /// @return Access mode selected by open().
        /// @note Meaningful only while isOpen() is true.
        [[nodiscard]] Types::File::Access access() const noexcept;
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
        [[nodiscard]] Types::Lock::Result tryLockShared() noexcept;
        /// @brief Attempts to acquire a non-blocking exclusive whole-file lock.
        /// @return Lock status, acquisition outcome, and active lock owner when acquired.
        [[nodiscard]] Types::Lock::Result tryLockExclusive() noexcept;

    private:
        std::unique_ptr<Detail::FileState> state_;
    };

    namespace Types::Lock
    {
        /// @brief Result returned by a non-blocking whole-file lock attempt.
        /// @details WouldBlock is a successful expected outcome with an inactive lock.
        struct Result
        {
            /// @brief Operation status. WouldBlock is reported through outcome, not as an error.
            IO::Types::Status status;
            /// @brief Whether the lock was acquired or could not be acquired immediately.
            Outcome outcome = Outcome::WouldBlock;
            /// @brief Active lock owner when outcome is Acquired; inactive otherwise.
            FileLock lock;
        };
    } // namespace Types::Lock

    /// @}

    // Whole-file operations
    // ------------------------------------------------------------

    /// @name Whole-file operations
    /// @{

    /// @brief Reads an entire file as bytes.
    /// @param path File path to read.
    /// @param options Open behavior, hard byte limit, and transfer buffer size.
    /// @return Collected bytes and final status. Partial data may be present after transfer or close failure.
    /// @note SizeLimitExceeded is a failed hard-limit result, not successful truncation.
    [[nodiscard]] IO::Types::ReadAllBytesResult readAllBytes(const Types::Path &path, const Types::File::ReadOptions &options = {}) noexcept;

    /// @brief Reads an entire file as strict UTF-8 text without BOM or line-ending transformation.
    /// @param path File path to read.
    /// @param options Open behavior, hard byte limit, and transfer buffer size.
    /// @return Valid UTF-8 text and final status. A failed read may preserve the complete valid UTF-8 prefix.
    [[nodiscard]] IO::Types::ReadAllTextResult readAllText(const Types::Path &path, const Types::File::ReadOptions &options = {}) noexcept;

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
        const Types::File::WriteOptions &options = {}) noexcept;

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
        const Types::File::WriteOptions &options = {}) noexcept
    {
        return writeAllBytes(path, std::span<const std::byte>(bytes.data(), bytes.size()), options);
    }

    /// @brief Writes exact strict UTF-8 text without BOM or line-ending transformation.
    /// @details UTF-8 is validated before parent creation, file creation, or truncation. Empty valid text still performs
    /// the requested create/truncate, flush, and close sequence.
    /// @param path Destination file path.
    /// @param utf8Text Exact UTF-8 text.
    /// @param options Creation, sharing, symlink, parent, and flush behavior.
    /// @return EncodingFailed with zero payload progress for malformed text, otherwise final status and accepted bytes.
    [[nodiscard]] IO::Types::WriteResult writeAllText(
        const Types::Path &path,
        std::string_view utf8Text,
        const Types::File::WriteOptions &options = {}) noexcept;

    /// @brief Appends bytes through an append-mode handle and reports accepted payload bytes.
    /// @details A flush or close failure preserves the payload byte count accepted before that failure.
    /// @param path Destination file path.
    /// @param bytes Payload bytes to append.
    /// @param options Creation, sharing, symlink, parent, and flush behavior.
    /// @return Final status and total payload bytes accepted.
    [[nodiscard]] IO::Types::WriteResult appendBytes(
        const Types::Path &path,
        std::span<const std::byte> bytes,
        const Types::File::AppendOptions &options = {}) noexcept;

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
        const Types::File::AppendOptions &options = {}) noexcept
    {
        return appendBytes(path, std::span<const std::byte>(bytes.data(), bytes.size()), options);
    }

    /// @brief Appends strict UTF-8 text without adding a line ending.
    /// @details UTF-8 is validated before parent or file creation. A flush or close failure preserves accepted payload progress.
    /// @param path Destination file path.
    /// @param utf8Text UTF-8 text to append.
    /// @param options Creation, sharing, symlink, parent, and flush behavior.
    /// @return EncodingFailed with zero payload progress for malformed text, otherwise final status and accepted bytes.
    [[nodiscard]] IO::Types::WriteResult appendText(
        const Types::Path &path,
        std::string_view utf8Text,
        const Types::File::AppendOptions &options = {}) noexcept;

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
        const Types::File::AtomicWriteOptions &options = {}) noexcept;

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
        const Types::File::AtomicWriteOptions &options = {}) noexcept
    {
        return writeAllBytesAtomic(path, std::span<const std::byte>(bytes.data(), bytes.size()), options);
    }

    /// @brief Atomically replaces exact strict UTF-8 contents.
    /// @param path Destination file path.
    /// @param utf8Text Exact replacement UTF-8 text.
    /// @param options Parent creation, replacement, symlink, flush, and temporary-name behavior.
    /// @return EncodingFailed before filesystem side effects for malformed text, otherwise the atomic-write status.
    [[nodiscard]] IO::Types::Status writeAllTextAtomic(
        const Types::Path &path,
        std::string_view utf8Text,
        const Types::File::AtomicWriteOptions &options = {}) noexcept;

    /// @brief Resizes an existing regular file.
    /// @param path File path to resize.
    /// @param sizeBytes Requested file size in bytes.
    /// @param options Symlink traversal behavior.
    /// @return Success or a validation, lookup, permission, or resize failure status.
    [[nodiscard]] IO::Types::Status resizeFile(
        const Types::Path &path,
        std::uint64_t sizeBytes,
        const Types::File::ResizeOptions &options = {}) noexcept;

    /// @brief Truncates an existing regular file to zero bytes.
    /// @param path File path to truncate.
    /// @param options Symlink traversal behavior.
    /// @return Success or a validation, lookup, permission, or resize failure status.
    [[nodiscard]] IO::Types::Status truncateFile(const Types::Path &path, const Types::File::ResizeOptions &options = {}) noexcept;

    /// @}

    /// @name Entry mutations
    /// @{

    /// @brief Copies one regular file.
    /// @param from Source file path.
    /// @param to Destination file path.
    /// @param options Replacement, symlink, parent, metadata, and flush behavior.
    /// @return Success or a validation, lookup, permission, copy, metadata, or flush failure status.
    [[nodiscard]] IO::Types::Status copyFile(const Types::Path &from, const Types::Path &to, const Types::File::CopyOptions &options = {}) noexcept;

    /// @brief Removes one regular file or symlink-to-file entry.
    /// @param path Path to remove.
    /// @param options Missing-target and symlink behavior.
    /// @return Success or a validation, lookup, type, permission, or removal failure status.
    [[nodiscard]] IO::Types::Status removeFile(const Types::Path &path, const Types::RemoveOptions &options = {}) noexcept;
} // namespace GameWIP::FileSystem
