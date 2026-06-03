/// @file io.h
/// @brief Public API for the GameWIP IO contract library.

#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

/// @brief Platform-neutral reader, writer, status, and whole-stream helper APIs.
namespace GameWIP::IO
{
    /// @brief Sentinel byte limit meaning no caller-imposed read limit.
    inline constexpr std::uint64_t kNoByteLimit = std::numeric_limits<std::uint64_t>::max();

    /// @brief Default temporary buffer size used by whole-stream helpers.
    inline constexpr std::size_t kDefaultBufferSize = 64 * 1024;

    /// @brief Passive IO data shapes.
    namespace Types
    {
        /// @brief Common error codes used by low-level IO contracts.
        enum class ErrorCode
        {
            /// @brief Operation completed successfully.
            Success,

            /// @brief An argument violates the operation contract.
            InvalidArgument,
            /// @brief The requested operation or capability is not supported.
            Unsupported,
            /// @brief The resource or memory helper is not open.
            NotOpen,
            /// @brief The resource is already open.
            AlreadyOpen,

            /// @brief The requested resource does not exist.
            NotFound,
            /// @brief The requested resource already exists.
            AlreadyExists,
            /// @brief The caller lacks permission for the requested operation.
            PermissionDenied,
            /// @brief A path exceeds a backend-supported limit.
            PathTooLong,

            /// @brief The requested resource is a directory when a non-directory was required.
            IsDirectory,
            /// @brief The requested resource is not a directory.
            NotDirectory,
            /// @brief The stream does not support seeking or a related position capability.
            NotSeekable,
            /// @brief The stream has reached end-of-stream where an explicit status is required.
            EndOfStream,

            /// @brief Opening a resource failed without a more specific portable code.
            OpenFailed,
            /// @brief Reading failed without a more specific portable code.
            ReadFailed,
            /// @brief Writing failed without a more specific portable code.
            WriteFailed,
            /// @brief Flushing failed without a more specific portable code.
            FlushFailed,
            /// @brief Closing failed without a more specific portable code.
            CloseFailed,
            /// @brief Seeking failed without a more specific portable code.
            SeekFailed,
            /// @brief Querying resource metadata failed.
            StatFailed,
            /// @brief Removing a resource failed.
            RemoveFailed,
            /// @brief Replacing a resource failed.
            ReplaceFailed,
            /// @brief Copying a resource failed.
            CopyFailed,
            /// @brief Creating a directory failed.
            DirectoryCreateFailed,
            /// @brief Listing a directory failed.
            DirectoryListFailed,

            /// @brief A read ended before the promised byte count was produced.
            PartialRead,
            /// @brief A write accepted fewer bytes than required before it failed.
            PartialWrite,
            /// @brief A requested, known, or observed size exceeds the accepted limit.
            SizeLimitExceeded,

            /// @brief The resource is busy or has an incompatible lock/share state.
            ResourceBusy,
            /// @brief Storage or quota capacity is exhausted.
            StorageFull,
            /// @brief A pipe or redirected stream no longer has a reader.
            BrokenPipe,
            /// @brief The operation was interrupted before completion.
            Interrupted,

            /// @brief Text encoding or conversion failed.
            EncodingFailed,
            /// @brief A backend-native failure has no more specific portable code.
            NativeFailure,
            /// @brief The failure category is unknown.
            Unknown
        };

        /// @brief Status returned by expected IO operations.
        struct Status
        {
            /// @brief Portable error category for the operation.
            ErrorCode code = ErrorCode::Success;
            /// @brief Backend-native error code when a concrete backend has one, otherwise zero.
            std::int64_t nativeCode = 0;
            /// @brief Developer-facing diagnostic text; not stable for machine parsing.
            std::string message;

            /// @brief Returns true when the operation succeeded.
            /// @return True for ErrorCode::Success.
            [[nodiscard]] bool ok() const noexcept
            {
                return code == ErrorCode::Success;
            }
        };

        /// @brief Origin used by seek operations.
        enum class SeekOrigin
        {
            /// @brief Interpret the offset relative to the beginning of the stream.
            Begin,
            /// @brief Interpret the offset relative to the current stream position.
            Current,
            /// @brief Interpret the offset relative to the end of the stream.
            End
        };

        /// @brief Flush strength requested by callers.
        enum class FlushMode
        {
            /// @brief Do not request an explicit flush.
            None,
            /// @brief Flush buffered data to the underlying resource where supported.
            Data,
            /// @brief Also request metadata durability where supported, without requiring it.
            DataAndMetadataBestEffort
        };

        /// @brief Result returned by byte read operations.
        struct ReadResult
        {
            /// @brief Operation status. Partial successful reads may still provide bytesRead.
            Status status;
            /// @brief Number of bytes copied into the caller destination.
            std::size_t bytesRead = 0;
            /// @brief True when the reader has reached the end of available input.
            bool endOfStream = false;
        };

        /// @brief Result returned by byte write operations.
        struct WriteResult
        {
            /// @brief Operation status. Partial successful writes may still provide bytesWritten.
            Status status;
            /// @brief Number of bytes accepted from the caller input.
            std::size_t bytesWritten = 0;
        };

        /// @brief Result returned by stream position operations.
        struct PositionResult
        {
            /// @brief Operation status.
            Status status;
            /// @brief Current stream position in bytes from the beginning when status is successful.
            std::uint64_t position = 0;
        };

        /// @brief Result returned by size query operations.
        struct SizeResult
        {
            /// @brief Operation status.
            Status status;
            /// @brief Total stream size in bytes when status is successful.
            std::uint64_t sizeBytes = 0;
        };

        /// @brief Result returned by whole-stream byte reads.
        struct ReadAllBytesResult
        {
            /// @brief Operation status.
            Status status;
            /// @brief Bytes collected before end-of-stream or failure.
            std::vector<std::byte> bytes;
        };

        /// @brief Result returned by whole-stream text reads.
        struct ReadAllTextResult
        {
            /// @brief Operation status.
            Status status;
            /// @brief Text bytes collected before end-of-stream or failure.
            std::string text;
        };
    } // namespace Types

    /// @brief Creates an IO status with portable, native, and diagnostic details.
    /// @param code Portable status code.
    /// @param nativeCode Backend-native error code, or zero when unavailable.
    /// @param message Developer-facing diagnostic text; not stable for machine parsing.
    /// @return Status containing the supplied values.
    /// @note Constructing a non-empty message may allocate; code-only statuses avoid that cost.
    [[nodiscard]] Types::Status makeStatus(Types::ErrorCode code, std::int64_t nativeCode = 0, std::string message = {});

    /// @brief Creates a successful IO status.
    /// @return Status whose code is ErrorCode::Success.
    [[nodiscard]] Types::Status successStatus();

    /// @brief Returns a stable string name for an ErrorCode value.
    /// @param code Error code to name.
    /// @return Stable non-owning string literal for known values, or "Unknown" for unknown enumerators.
    [[nodiscard]] std::string_view errorCodeName(Types::ErrorCode code) noexcept;

    /// @brief Movable abstract byte reader contract used by generic IO helpers.
    ///
    /// Contract:
    /// `read()` reports byte count and end-of-stream independently. Optional capabilities return
    /// `NotSeekable` or `Unsupported` when unavailable. Expected IO failures return status values.
    ///
    /// Thread-safety:
    /// Different Reader objects may be used concurrently. The same object is not thread-safe unless
    /// the concrete implementation explicitly documents otherwise.
    class Reader
    {
    public:
        /// @brief Creates a stateless reader base.
        Reader() = default;

        /// @brief Destroys the reader without throwing.
        virtual ~Reader() noexcept = default;

        /// @brief Reader objects are not copy-constructible.
        Reader(const Reader &) = delete;

        /// @brief Reader objects are not copy-assignable.
        Reader &operator=(const Reader &) = delete;

        /// @brief Move-constructs the reader base.
        Reader(Reader &&) noexcept = default;

        /// @brief Move-assigns the reader base.
        Reader &operator=(Reader &&) noexcept = default;

        /// @brief Returns whether this reader currently has readable state.
        /// @return True for stateless readers by default.
        [[nodiscard]] virtual bool isOpen() const noexcept;

        /// @brief Returns whether this reader currently supports seek operations.
        /// @return False by default.
        [[nodiscard]] virtual bool canSeek() const noexcept;

        /// @brief Reads bytes into caller-owned memory.
        /// @param destination Destination memory to fill.
        /// @return Read status, byte count, and end-of-stream state.
        /// @note bytesRead must never exceed destination.size().
        [[nodiscard]] virtual Types::ReadResult read(std::span<std::byte> destination) = 0;

        /// @brief Closes the reader when it owns closeable state.
        /// @return Success or a close failure status.
        [[nodiscard]] virtual Types::Status close();

        /// @brief Returns the current stream position when supported.
        /// @return Current position, or NotSeekable for non-position-aware readers.
        [[nodiscard]] virtual Types::PositionResult position() const;

        /// @brief Returns the stream size when supported.
        /// @return Stream size, or NotSeekable for non-size-aware readers.
        [[nodiscard]] virtual Types::SizeResult size() const;

        /// @brief Moves the stream position when supported.
        /// @param offset Signed offset relative to origin.
        /// @param origin Origin used to interpret offset.
        /// @return Success or a seek-related failure status.
        [[nodiscard]] virtual Types::Status seek(std::int64_t offset, Types::SeekOrigin origin);
    };

    /// @brief Movable abstract byte writer contract used by generic IO helpers.
    ///
    /// Contract:
    /// `write()` reports the accepted byte count independently from failure status. Optional
    /// capabilities return `NotSeekable` or `Unsupported` when unavailable. Expected IO failures
    /// return status values.
    ///
    /// Thread-safety:
    /// Different Writer objects may be used concurrently. The same object is not thread-safe unless
    /// the concrete implementation explicitly documents otherwise.
    class Writer
    {
    public:
        /// @brief Creates a stateless writer base.
        Writer() = default;

        /// @brief Destroys the writer without throwing.
        virtual ~Writer() noexcept = default;

        /// @brief Writer objects are not copy-constructible.
        Writer(const Writer &) = delete;

        /// @brief Writer objects are not copy-assignable.
        Writer &operator=(const Writer &) = delete;

        /// @brief Move-constructs the writer base.
        Writer(Writer &&) noexcept = default;

        /// @brief Move-assigns the writer base.
        Writer &operator=(Writer &&) noexcept = default;

        /// @brief Returns whether this writer currently has writable state.
        /// @return True for stateless writers by default.
        [[nodiscard]] virtual bool isOpen() const noexcept;

        /// @brief Returns whether this writer currently supports seek operations.
        /// @return False by default.
        [[nodiscard]] virtual bool canSeek() const noexcept;

        /// @brief Writes bytes from caller-owned memory.
        /// @param bytes Bytes to write.
        /// @return Write status and byte count.
        /// @note bytesWritten must never exceed bytes.size().
        [[nodiscard]] virtual Types::WriteResult write(std::span<const std::byte> bytes) = 0;

        /// @brief Flushes buffered data when the writer owns flushable state.
        /// @param mode Requested flush strength.
        /// @return Success or a flush failure status.
        [[nodiscard]] virtual Types::Status flush(Types::FlushMode mode = Types::FlushMode::Data);

        /// @brief Closes the writer when it owns closeable state.
        /// @return Success or a close failure status.
        [[nodiscard]] virtual Types::Status close();

        /// @brief Returns the current stream position when supported.
        /// @return Current position, or NotSeekable for non-position-aware writers.
        [[nodiscard]] virtual Types::PositionResult position() const;

        /// @brief Moves the stream position when supported.
        /// @param offset Signed offset relative to origin.
        /// @param origin Origin used to interpret offset.
        /// @return Success or a seek-related failure status.
        [[nodiscard]] virtual Types::Status seek(std::int64_t offset, Types::SeekOrigin origin);
    };

    /// @brief Non-owning Reader over existing contiguous byte storage.
    /// @details The source storage must remain alive and at a stable address while this reader is used.
    /// Temporary std::string and std::vector storage is rejected to prevent immediate dangling views.
    /// @note MemoryReader is seekable only while open and performs no allocation or operating-system calls.
    class MemoryReader final : public Reader
    {
    public:
        /// @brief Creates a reader over caller-owned bytes.
        /// @param bytes Bytes to read. The caller must keep this memory alive while the reader is used.
        explicit MemoryReader(std::span<const std::byte> bytes) noexcept;

        /// @brief Creates a reader over caller-owned UTF-8 text bytes.
        /// @param text Text bytes to read. The caller must keep this string view storage alive while the reader is used.
        explicit MemoryReader(std::string_view text) noexcept;

        /// @brief Rejects temporary std::string storage that would leave the reader dangling.
        /// @tparam String Exact std::string rvalue type.
        /// @param text Temporary text storage.
        template <typename String>
            requires(std::same_as<std::remove_cvref_t<String>, std::string> && std::is_rvalue_reference_v<String &&>)
        explicit MemoryReader(String &&text) = delete;

        /// @brief Creates a reader over caller-owned vector storage.
        /// @tparam Allocator Vector allocator type.
        /// @param bytes Bytes to read. The caller must keep this vector alive while the reader is used.
        template <typename Allocator>
        explicit MemoryReader(const std::vector<std::byte, Allocator> &bytes) noexcept
            : MemoryReader(std::span<const std::byte>(bytes.data(), bytes.size()))
        {
        }

        /// @brief Rejects temporary vector storage that would leave the reader dangling.
        /// @tparam Allocator Vector allocator type.
        /// @param bytes Temporary byte storage.
        template <typename Allocator> explicit MemoryReader(std::vector<std::byte, Allocator> &&bytes) = delete;

        /// @brief Rejects temporary const vector storage that would leave the reader dangling.
        /// @tparam Allocator Vector allocator type.
        /// @param bytes Temporary byte storage.
        template <typename Allocator> explicit MemoryReader(const std::vector<std::byte, Allocator> &&bytes) = delete;

        /// @brief Returns whether the memory reader is open.
        /// @return True until close() is called.
        [[nodiscard]] bool isOpen() const noexcept override;

        /// @brief Returns whether seek operations are currently available.
        /// @return The same value as isOpen().
        [[nodiscard]] bool canSeek() const noexcept override;

        /// @brief Reads from the current position into caller-owned memory.
        /// @param destination Destination memory to fill.
        /// @return Read status, byte count, and end-of-stream state.
        /// @note Destination may overlap the source memory.
        [[nodiscard]] Types::ReadResult read(std::span<std::byte> destination) override;

        /// @brief Closes this reader without affecting caller-owned source storage.
        /// @return Success. Repeated close calls also succeed.
        [[nodiscard]] Types::Status close() override;

        /// @brief Returns the current byte position while open.
        /// @return Current position, or NotOpen after close().
        [[nodiscard]] Types::PositionResult position() const override;

        /// @brief Returns the source byte count while open.
        /// @return Source size, or NotOpen after close().
        [[nodiscard]] Types::SizeResult size() const override;

        /// @brief Moves the current position within the source byte range.
        /// @param offset Signed offset relative to origin.
        /// @param origin Origin used to interpret offset.
        /// @return Success, NotOpen, InvalidArgument, SizeLimitExceeded, or SeekFailed.
        [[nodiscard]] Types::Status seek(std::int64_t offset, Types::SeekOrigin origin) override;

    private:
        std::span<const std::byte> bytes_;
        std::size_t position_ = 0;
        bool open_ = true;
    };

    /// @brief Owning Writer that appends bytes to memory.
    /// @details MemoryWriter remains non-seekable. Collected output remains inspectable, clearable,
    /// reservable, and extractable after close(), but write(), flush(), and position() require open state.
    /// @note MemoryWriter is not internally synchronized and performs no operating-system calls.
    class MemoryWriter final : public Writer
    {
    public:
        /// @brief Creates an open writer with empty storage.
        MemoryWriter() = default;

        /// @brief Creates a memory writer with reserved capacity.
        /// @param initialCapacity Initial byte capacity to reserve.
        /// @throws std::bad_alloc When the requested capacity cannot be allocated.
        /// @throws std::length_error When initialCapacity exceeds the vector maximum size.
        explicit MemoryWriter(std::size_t initialCapacity);

        /// @brief Returns whether the memory writer accepts writes and flushes.
        /// @return True until close() is called.
        [[nodiscard]] bool isOpen() const noexcept override;

        /// @brief Returns whether seek operations are available.
        /// @return False; MemoryWriter is append-only.
        [[nodiscard]] bool canSeek() const noexcept override;

        /// @brief Appends bytes, including spans that refer to this writer's current bytes.
        /// @param bytes Bytes to append.
        /// @return Write status and byte count.
        [[nodiscard]] Types::WriteResult write(std::span<const std::byte> bytes) override;

        /// @brief Validates open state; memory-backed writes require no physical flush.
        /// @param mode Requested flush strength.
        /// @return Success while open, or NotOpen after close().
        [[nodiscard]] Types::Status flush(Types::FlushMode mode = Types::FlushMode::Data) override;

        /// @brief Closes this writer without discarding collected output.
        /// @return Success. Repeated close calls also succeed.
        [[nodiscard]] Types::Status close() override;

        /// @brief Returns the append position while open.
        /// @return Current byte count as the position, or NotOpen after close().
        [[nodiscard]] Types::PositionResult position() const override;

        /// @brief Writes bytes from vector storage.
        /// @tparam Allocator Vector allocator type.
        /// @param bytes Bytes to append.
        /// @return Write status and byte count.
        template <typename Allocator> [[nodiscard]] Types::WriteResult write(const std::vector<std::byte, Allocator> &bytes)
        {
            return write(std::span<const std::byte>(bytes.data(), bytes.size()));
        }

        /// @brief Reserves at least the requested byte capacity.
        /// @param capacity Byte capacity to reserve.
        /// @throws std::bad_alloc When the requested capacity cannot be allocated.
        /// @throws std::length_error When capacity exceeds the vector maximum size.
        void reserve(std::size_t capacity);

        /// @brief Returns a read-only view of the bytes written so far.
        /// @return Byte view owned by this writer.
        [[nodiscard]] std::span<const std::byte> bytes() const noexcept;

        /// @brief Returns collected bytes interpreted as a string of UTF-8 bytes.
        /// @return Copy of collected bytes as std::string. No UTF-8 validation is performed.
        /// @throws std::bad_alloc When the returned string cannot be allocated.
        /// @throws std::length_error When the collected byte count exceeds the string maximum size.
        [[nodiscard]] std::string text() const;

        /// @brief Moves collected bytes out of the writer and replaces its storage with an empty vector.
        /// @return Collected byte vector.
        /// @note The writer remains open or closed according to its state before this call.
        /// @note Transferring ownership may discard the writer's previously reserved capacity.
        [[nodiscard]] std::vector<std::byte> takeBytes() noexcept;

        /// @brief Returns the number of bytes written so far.
        /// @return Current byte count.
        [[nodiscard]] std::size_t size() const noexcept;

        /// @brief Returns current reserved byte capacity.
        /// @return Current vector capacity in bytes.
        [[nodiscard]] std::size_t capacity() const noexcept;

        /// @brief Returns whether no bytes have been written.
        /// @return True when size() is zero.
        [[nodiscard]] bool empty() const noexcept;

        /// @brief Clears written bytes while preserving allocated capacity.
        void clear() noexcept;

    private:
        std::vector<std::byte> bytes_;
        bool open_ = true;
    };

    /// @brief Reads all bytes from a reader until end-of-stream or failure.
    /// @param reader Reader to drain.
    /// @param maxBytes Maximum accepted stream size, or kNoByteLimit for no caller limit.
    /// @param bufferSize Temporary transfer buffer size. Must be greater than zero.
    /// @return Collected bytes and final status. SizeLimitExceeded is returned if the stream exceeds maxBytes.
    /// @note At a finite limit, an unknown-size reader may be advanced by one extra byte to determine whether more data exists.
    [[nodiscard]] Types::ReadAllBytesResult readAllBytes(
        Reader &reader,
        std::uint64_t maxBytes = kNoByteLimit,
        std::size_t bufferSize = kDefaultBufferSize);

    /// @brief Reads all bytes using caller-owned temporary storage for unknown-size readers.
    /// @param reader Reader to drain.
    /// @param scratchBuffer Temporary transfer buffer. Must not be empty.
    /// @param maxBytes Maximum accepted stream size, or kNoByteLimit for no caller limit.
    /// @return Collected bytes and final status. SizeLimitExceeded is returned if the stream exceeds maxBytes.
    /// @note At a finite limit, an unknown-size reader may be advanced by one extra byte to determine whether more data exists.
    [[nodiscard]] Types::ReadAllBytesResult readAllBytes(Reader &reader, std::span<std::byte> scratchBuffer, std::uint64_t maxBytes = kNoByteLimit);

    /// @brief Reads all text bytes from a reader as UTF-8 text bytes.
    /// @param reader Reader to drain.
    /// @param maxBytes Maximum accepted stream size, or kNoByteLimit for no caller limit.
    /// @param bufferSize Temporary transfer buffer size. Must be greater than zero.
    /// @return Collected text bytes and final status. SizeLimitExceeded is returned if the stream exceeds maxBytes.
    /// @note At a finite limit, an unknown-size reader may be advanced by one extra byte to determine whether more data exists.
    [[nodiscard]] Types::ReadAllTextResult readAllText(
        Reader &reader,
        std::uint64_t maxBytes = kNoByteLimit,
        std::size_t bufferSize = kDefaultBufferSize);

    /// @brief Reads all text bytes using caller-owned temporary storage for unknown-size readers.
    /// @param reader Reader to drain.
    /// @param scratchBuffer Temporary transfer buffer. Must not be empty.
    /// @param maxBytes Maximum accepted stream size, or kNoByteLimit for no caller limit.
    /// @return Collected text bytes and final status. SizeLimitExceeded is returned if the stream exceeds maxBytes.
    /// @note At a finite limit, an unknown-size reader may be advanced by one extra byte to determine whether more data exists.
    [[nodiscard]] Types::ReadAllTextResult readAllText(Reader &reader, std::span<std::byte> scratchBuffer, std::uint64_t maxBytes = kNoByteLimit);

    /// @brief Writes all bytes to a writer, retrying partial writes until complete or failed.
    /// @param writer Writer to drain bytes into.
    /// @param bytes Bytes to write.
    /// @return Success when every byte was accepted; otherwise the failing status.
    [[nodiscard]] Types::Status writeAllBytes(Writer &writer, std::span<const std::byte> bytes);

    /// @brief Writes all vector bytes to a writer, retrying partial writes until complete or failed.
    /// @tparam Allocator Vector allocator type.
    /// @param writer Writer to drain bytes into.
    /// @param bytes Bytes to write.
    /// @return Success when every byte was accepted; otherwise the failing status.
    template <typename Allocator> [[nodiscard]] Types::Status writeAllBytes(Writer &writer, const std::vector<std::byte, Allocator> &bytes)
    {
        return writeAllBytes(writer, std::span<const std::byte>(bytes.data(), bytes.size()));
    }

    /// @brief Writes UTF-8 text bytes to a writer.
    /// @param writer Writer to drain text into.
    /// @param utf8Text Text bytes to write.
    /// @return Success when every byte was accepted; otherwise the failing status.
    [[nodiscard]] Types::Status writeAllText(Writer &writer, std::string_view utf8Text);
} // namespace GameWIP::IO
