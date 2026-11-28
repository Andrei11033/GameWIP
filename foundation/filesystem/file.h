/// @file file.h
/// @brief Public file handles, locks, and whole-file operations.

#pragma once

#include "filesystem/file_types.h"
#include "io/transfer.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace GameWIP::FileSystem
{
    namespace Internal
    {
        struct FileState;
        struct FileLockState;
    } // namespace Internal

    class File;
    class FileReader;
    class FileWriter;
    class FileLock;

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
        FileLock(const FileLock &) = delete;
        FileLock &operator=(const FileLock &) = delete;
        /// @brief Move-constructs a lock and transfers unlock responsibility.
        /// @param other Lock owner to empty.
        FileLock(FileLock &&other) noexcept;
        /// @brief Move assignment is disabled to avoid hiding an unlock failure.
        FileLock &operator=(FileLock &&other) = delete;
        /// @brief Releases an active lock on a best-effort basis without throwing.
        /// @note Use unlock() when release failure must be observed.
        ~FileLock() noexcept;

        /// @brief Reports whether this object still owns an active lock.
        /// @return True until the lock is moved from or successfully unlocked.
        [[nodiscard]] bool active() const noexcept;
        /// @brief Returns the mode selected when the lock was acquired.
        /// @return Shared or exclusive mode selected during acquisition.
        /// @note Meaningful only while active() is true.
        [[nodiscard]] Types::Lock::Mode mode() const noexcept;
        /// @brief Releases the lock. A failed unlock remains active and may be retried.
        /// @return Success, UnlockFailed, or a more specific backend status.
        [[nodiscard]] GameWIP::Status::Types::Result<void> unlock() noexcept;

    private:
        friend class File;
        friend class FileReader;
        friend class FileWriter;

        explicit FileLock(std::unique_ptr<Internal::FileLockState> state, Types::Lock::Mode mode) noexcept;

        std::unique_ptr<Internal::FileLockState> state_;
        Types::Lock::Mode mode_ = Types::Lock::Mode::Shared;
    };

    /// @}

    /// @name File streams
    /// @{

    /// @brief Read-only file-backed IO reader.
    /// @details Move construction transfers ownership. Move assignment is deleted because replacing an open handle could hide close failure.
    class FileReader final : public IO::Reader
    {
    public:
        /// @brief Creates a closed reader.
        FileReader() noexcept;
        FileReader(const FileReader &) = delete;
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
        [[nodiscard]] GameWIP::Status::Types::Result<void> open(const Types::Path &path, const Types::File::ReaderOpenOptions &options = {}) noexcept;
        /// @brief Reports whether a file is open.
        /// @return True after a successful open() and before close().
        [[nodiscard]] bool isOpen() const noexcept override;
        /// @brief Reports whether normal file-position changes are available.
        /// @return True while a normal file handle is open.
        [[nodiscard]] bool canSeek() const noexcept override;
        /// @brief Reads bytes from the current file position.
        /// @param destination Caller-owned call-scoped memory; the reader does not retain it.
        /// @return Read status, copied byte count, and end-of-stream state. A successful final read may contain bytes and set endOfStream.
        /// @note An empty destination performs no transfer and queries the current end-of-stream state.
        [[nodiscard]] GameWIP::Status::Types::Result<IO::Types::ReadProgress> read(std::span<std::byte> destination) noexcept override;
        /// @brief Closes the file. Repeated close calls succeed.
        /// @return ResourceBusy while a lock acquired from this reader remains active, or a close failure status.
        /// @note Failure leaves the reader open so close() can be retried.
        [[nodiscard]] GameWIP::Status::Types::Result<void> close() noexcept override;
        /// @brief Returns the current byte position.
        /// @return Current position, or NotOpen while closed.
        [[nodiscard]] GameWIP::Status::Types::Result<std::uint64_t> position() const noexcept override;
        /// @brief Returns the current file size.
        /// @return File size, or NotOpen while closed.
        [[nodiscard]] GameWIP::Status::Types::Result<std::uint64_t> size() const noexcept override;
        /// @brief Moves the current file position.
        /// @param offset Signed offset relative to origin.
        /// @param origin Origin used to interpret offset.
        /// @return Success, NotOpen, InvalidArgument, or a seek failure status.
        [[nodiscard]] GameWIP::Status::Types::Result<void> seek(std::int64_t offset, IO::Types::SeekOrigin origin) noexcept override;
        /// @brief Attempts to acquire a non-blocking shared whole-file lock.
        /// @return Acquired lock, or a failure status such as WouldBlock.
        [[nodiscard]] GameWIP::Status::Types::Result<FileLock> tryLockShared() noexcept;

    private:
        std::unique_ptr<Internal::FileState> state_;
    };

    /// @brief Write-only file-backed IO writer.
    /// @details Move construction transfers ownership. Move assignment is deleted because replacing an open handle could hide flush or close failure.
    class FileWriter final : public IO::Writer
    {
    public:
        /// @brief Creates a closed writer.
        FileWriter() noexcept;
        FileWriter(const FileWriter &) = delete;
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
        [[nodiscard]] GameWIP::Status::Types::Result<void> open(const Types::Path &path, const Types::File::WriterOpenOptions &options = {}) noexcept;
        /// @brief Reports whether a file is open.
        /// @return True after a successful open() and before close().
        [[nodiscard]] bool isOpen() const noexcept override;
        /// @brief Reports whether normal file-position changes are available.
        /// @return False for append modes and while closed; true for other open modes.
        [[nodiscard]] bool canSeek() const noexcept override;
        /// @brief Writes bytes at the current position or current end in append mode.
        /// @param bytes Caller-owned call-scoped bytes; the writer does not retain them.
        /// @return Write status and accepted byte count. Success can report fewer bytes than requested.
        /// @note Empty input succeeds without transferring bytes.
        [[nodiscard]] GameWIP::Status::Types::Result<IO::Types::WriteProgress> write(std::span<const std::byte> bytes) noexcept override;
        /// @brief Flushes written data according to the requested strength.
        /// @param mode Requested flush strength. None validates state but requests no physical flush.
        /// @return Success, NotOpen, InvalidArgument, or a flush failure status.
        [[nodiscard]] GameWIP::Status::Types::Result<void> flush(IO::Types::FlushMode mode = IO::Types::FlushMode::Data) noexcept override;
        /// @brief Applies configured flush-on-close and closes the file. Repeated close calls succeed.
        /// @return ResourceBusy while a lock acquired from this writer remains active, or a flush/close failure status.
        /// @note Failure leaves the writer open so close() can be retried.
        [[nodiscard]] GameWIP::Status::Types::Result<void> close() noexcept override;
        /// @brief Returns the current byte position when meaningful.
        /// @return Current position, NotOpen, or NotSeekable for append mode.
        [[nodiscard]] GameWIP::Status::Types::Result<std::uint64_t> position() const noexcept override;
        /// @brief Returns the current file size.
        /// @return File size, or NotOpen while closed.
        [[nodiscard]] GameWIP::Status::Types::Result<std::uint64_t> size() const noexcept;
        /// @brief Moves the current file position outside append mode.
        /// @param offset Signed offset relative to origin.
        /// @param origin Origin used to interpret offset.
        /// @return Success, NotOpen, NotSeekable, InvalidArgument, or a seek failure status.
        [[nodiscard]] GameWIP::Status::Types::Result<void> seek(std::int64_t offset, IO::Types::SeekOrigin origin) noexcept override;
        /// @brief Attempts to acquire a non-blocking exclusive whole-file lock.
        /// @return Acquired lock, or a failure status such as WouldBlock.
        [[nodiscard]] GameWIP::Status::Types::Result<FileLock> tryLockExclusive() noexcept;

    private:
        std::unique_ptr<Internal::FileState> state_;
    };

    /// @brief Read/write file-backed IO stream for modification workflows.
    /// @details Move construction transfers ownership. Move assignment is deleted because replacing an open handle could hide flush or close failure.
    class File final : public IO::Reader, public IO::Writer
    {
    public:
        /// @brief Creates a closed read/write file.
        File() noexcept;
        File(const File &) = delete;
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
        [[nodiscard]] GameWIP::Status::Types::Result<void> open(const Types::Path &path, const Types::File::OpenOptions &options = {}) noexcept;
        /// @brief Reports whether a file is open.
        /// @return True after a successful open() and before close().
        [[nodiscard]] bool isOpen() const noexcept override;
        /// @brief Reports whether normal file-position changes are available.
        /// @return True while a normal file handle is open.
        [[nodiscard]] bool canSeek() const noexcept override;
        /// @brief Returns the access mode selected by open().
        /// @return Access mode selected by open().
        /// @note Meaningful only while isOpen() is true.
        [[nodiscard]] Types::File::Access access() const noexcept;
        /// @brief Reads bytes from the current file position.
        /// @param destination Caller-owned call-scoped memory; the reader does not retain it.
        /// @return Read status, copied byte count, and end-of-stream state. A successful final read may contain bytes and set endOfStream.
        /// @note An empty destination performs no transfer and queries the current end-of-stream state.
        [[nodiscard]] GameWIP::Status::Types::Result<IO::Types::ReadProgress> read(std::span<std::byte> destination) noexcept override;
        /// @brief Writes bytes at the current position or current end in append mode.
        /// @param bytes Caller-owned call-scoped bytes; the file does not retain them.
        /// @return Write status and accepted byte count.
        [[nodiscard]] GameWIP::Status::Types::Result<IO::Types::WriteProgress> write(std::span<const std::byte> bytes) noexcept override;
        /// @brief Flushes written data according to the requested strength.
        /// @param mode Requested flush strength. None validates state but requests no physical flush.
        /// @return Success, NotOpen, InvalidArgument, or a flush failure status.
        [[nodiscard]] GameWIP::Status::Types::Result<void> flush(IO::Types::FlushMode mode = IO::Types::FlushMode::Data) noexcept override;
        /// @brief Applies configured flush-on-close and closes the file. Repeated close calls succeed.
        /// @return ResourceBusy while a lock acquired from this file remains active, or a flush/close failure status.
        /// @note Failure leaves the file open so close() can be retried.
        [[nodiscard]] GameWIP::Status::Types::Result<void> close() noexcept override;
        /// @brief Returns the current byte position.
        /// @return Current position, or NotOpen while closed.
        [[nodiscard]] GameWIP::Status::Types::Result<std::uint64_t> position() const noexcept override;
        /// @brief Returns the current file size.
        /// @return File size, or NotOpen while closed.
        [[nodiscard]] GameWIP::Status::Types::Result<std::uint64_t> size() const noexcept override;
        /// @brief Moves the current file position.
        /// @param offset Signed offset relative to origin.
        /// @param origin Origin used to interpret offset.
        /// @return Success, NotOpen, InvalidArgument, or a seek failure status.
        [[nodiscard]] GameWIP::Status::Types::Result<void> seek(std::int64_t offset, IO::Types::SeekOrigin origin) noexcept override;
        /// @brief Resizes the open file.
        /// @param sizeBytes Requested file size in bytes.
        /// @return Success, NotOpen, PermissionDenied for read-only access, SizeLimitExceeded, or a resize failure status.
        /// @note On success, the previous position is restored when it still fits; otherwise a shrink leaves the position at the new end.
        /// If restoring that position fails after the native length update, resize() reports the failure even though
        /// the requested length already applies.
        [[nodiscard]] GameWIP::Status::Types::Result<void> resize(std::uint64_t sizeBytes) noexcept;
        /// @brief Attempts to acquire a non-blocking shared whole-file lock.
        /// @return Acquired lock, or a failure status such as WouldBlock.
        [[nodiscard]] GameWIP::Status::Types::Result<FileLock> tryLockShared() noexcept;
        /// @brief Attempts to acquire a non-blocking exclusive whole-file lock.
        /// @return Acquired lock, or a failure status such as WouldBlock.
        [[nodiscard]] GameWIP::Status::Types::Result<FileLock> tryLockExclusive() noexcept;

    private:
        friend GameWIP::Status::Types::Result<void> resizeFile(
            const Types::Path &path,
            std::uint64_t sizeBytes,
            const Types::File::ResizeOptions &options) noexcept;

        /// @brief Resizes a private path-helper handle without preserving its unobservable position.
        [[nodiscard]] GameWIP::Status::Types::Result<void> resizeWithoutPositionRestore(std::uint64_t sizeBytes) noexcept;

        std::unique_ptr<Internal::FileState> state_;
    };

    /// @}

    /// @name Whole-file operations
    /// @{

    /// @brief Reads an entire file as bytes.
    /// @return Collected bytes and final status; partial data may accompany a failure.
    /// @see @ref filesystem_whole_file_io
    [[nodiscard]] GameWIP::Status::Types::Result<std::vector<std::byte>> readAllBytes(
        const Types::Path &path,
        const Types::File::ReadOptions &options = {}) noexcept;

    /// @brief Reads an entire file as strict UTF-8 text.
    /// @return Validated text and final status; a failed read may keep the valid prefix.
    /// @see @ref io_utf8_policy
    [[nodiscard]] GameWIP::Status::Types::Result<std::string> readAllText(
        const Types::Path &path,
        const Types::File::ReadOptions &options = {}) noexcept;

    /// @brief Writes exact contents non-atomically.
    /// @details Empty input still performs create/truncate, flush, and close.
    /// @return Final status and payload bytes accepted, preserved across a later flush or close failure.
    [[nodiscard]] GameWIP::Status::Types::Result<IO::Types::WriteProgress> writeAllBytes(
        const Types::Path &path,
        std::span<const std::byte> bytes,
        const Types::File::WriteOptions &options = {}) noexcept;

    /// @brief Writes exact vector contents; forwards to the span overload.
    /// @tparam Allocator Byte-vector allocator type.
    template <typename Allocator>
    [[nodiscard]] GameWIP::Status::Types::Result<IO::Types::WriteProgress> writeAllBytes(
        const Types::Path &path,
        const std::vector<std::byte, Allocator> &bytes,
        const Types::File::WriteOptions &options = {}) noexcept
    {
        return writeAllBytes(path, std::span<const std::byte>(bytes.data(), bytes.size()), options);
    }

    /// @brief Writes exact UTF-8 text without BOM or line-ending transformation.
    /// @return EncodingFailed with zero progress and no side effect for invalid text; otherwise as writeAllBytes().
    /// @see @ref io_utf8_policy
    [[nodiscard]] GameWIP::Status::Types::Result<IO::Types::WriteProgress> writeAllText(
        const Types::Path &path,
        std::string_view utf8Text,
        const Types::File::WriteOptions &options = {}) noexcept;

    /// @brief Appends bytes through an append-mode handle.
    /// @return Final status and payload bytes accepted, preserved across a later flush or close failure.
    [[nodiscard]] GameWIP::Status::Types::Result<IO::Types::WriteProgress> appendBytes(
        const Types::Path &path,
        std::span<const std::byte> bytes,
        const Types::File::AppendOptions &options = {}) noexcept;

    /// @brief Appends vector contents; forwards to the span overload.
    /// @tparam Allocator Byte-vector allocator type.
    template <typename Allocator>
    [[nodiscard]] GameWIP::Status::Types::Result<IO::Types::WriteProgress> appendBytes(
        const Types::Path &path,
        const std::vector<std::byte, Allocator> &bytes,
        const Types::File::AppendOptions &options = {}) noexcept
    {
        return appendBytes(path, std::span<const std::byte>(bytes.data(), bytes.size()), options);
    }

    /// @brief Appends UTF-8 text without adding a line ending.
    /// @return EncodingFailed with zero progress and no side effect for invalid text; otherwise as appendBytes().
    /// @see @ref io_utf8_policy
    [[nodiscard]] GameWIP::Status::Types::Result<IO::Types::WriteProgress> appendText(
        const Types::Path &path,
        std::string_view utf8Text,
        const Types::File::AppendOptions &options = {}) noexcept;

    /// @brief Atomically replaces file contents through a same-directory temporary file.
    /// @return Success or the validation, temporary-file, write, flush, or replacement failure.
    /// @see @ref filesystem_atomic_write
    [[nodiscard]] GameWIP::Status::Types::Result<void> writeAllBytesAtomic(
        const Types::Path &path,
        std::span<const std::byte> bytes,
        const Types::File::AtomicWriteOptions &options = {}) noexcept;

    /// @brief Atomically replaces contents from vector storage; forwards to the span overload.
    /// @tparam Allocator Byte-vector allocator type.
    template <typename Allocator>
    [[nodiscard]] GameWIP::Status::Types::Result<void> writeAllBytesAtomic(
        const Types::Path &path,
        const std::vector<std::byte, Allocator> &bytes,
        const Types::File::AtomicWriteOptions &options = {}) noexcept
    {
        return writeAllBytesAtomic(path, std::span<const std::byte>(bytes.data(), bytes.size()), options);
    }

    /// @brief Atomically replaces contents with UTF-8 text.
    /// @return EncodingFailed before any side effect for invalid text; otherwise the atomic-write status.
    /// @see @ref io_utf8_policy
    [[nodiscard]] GameWIP::Status::Types::Result<void> writeAllTextAtomic(
        const Types::Path &path,
        std::string_view utf8Text,
        const Types::File::AtomicWriteOptions &options = {}) noexcept;

    /// @brief Resizes an existing regular file.
    /// @param path File path to resize.
    /// @param sizeBytes Requested file size in bytes.
    /// @param options Symlink traversal behavior.
    /// @return Success or a validation, lookup, permission, resize, or close failure status.
    /// @note The helper does not report restoration failure for its private temporary handle. A failed resize or close can still leave the requested
    /// length applied.
    [[nodiscard]] GameWIP::Status::Types::Result<void> resizeFile(
        const Types::Path &path,
        std::uint64_t sizeBytes,
        const Types::File::ResizeOptions &options = {}) noexcept;

    /// @brief Truncates an existing regular file to zero bytes.
    /// @param path File path to truncate.
    /// @param options Symlink traversal behavior.
    /// @return Success or a validation, lookup, permission, resize, or close failure status.
    /// @note The helper does not report restoration failure for its private temporary handle. A failed resize or close can still leave the requested
    /// length applied.
    [[nodiscard]] GameWIP::Status::Types::Result<void> truncateFile(const Types::Path &path, const Types::File::ResizeOptions &options = {}) noexcept;

    /// @}

    /// @name Entry mutations
    /// @{

    /// @brief Copies one regular file.
    /// @param from Source file path.
    /// @param to Destination file path.
    /// @param options Replacement, symlink, parent, metadata, and flush behavior.
    /// @return Success or a validation, lookup, permission, copy, metadata, or flush failure status.
    /// @note Copying is non-atomic: a failure after destination creation can leave destination content changed.
    [[nodiscard]] GameWIP::Status::Types::Result<void> copyFile(
        const Types::Path &from,
        const Types::Path &to,
        const Types::File::CopyOptions &options = {}) noexcept;

    /// @brief Removes one regular file or symlink-to-file entry.
    /// @param path Path to remove.
    /// @param options Missing-target and symlink behavior.
    /// @return Success or a validation, lookup, type, permission, or removal failure status.
    [[nodiscard]] GameWIP::Status::Types::Result<void> removeFile(const Types::Path &path, const Types::RemoveOptions &options = {}) noexcept;
} // namespace GameWIP::FileSystem
