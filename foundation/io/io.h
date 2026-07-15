/// @file io.h
/// @brief Public status, reader, writer, memory-stream, and whole-transfer APIs for GameWIP IO.

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

/// @brief Platform-neutral byte-transfer contracts shared by low-level GameWIP libraries.
/// @details IO defines portable status/result shapes and resource-agnostic transfer interfaces. It
/// does not open operating-system resources or provide a platform backend.
namespace GameWIP::IO
{
    /// @brief Sentinel meaning a whole-stream read has no caller-imposed byte limit.
    /// @note Result-container, address-space, and allocation limits still apply.
    inline constexpr std::uint64_t kNoByteLimit = std::numeric_limits<std::uint64_t>::max();

    /// @brief Default temporary buffer size for internally buffered unknown-size reads.
    /// @note Known-size reads allocate their final output directly and do not use this buffer size.
    inline constexpr std::size_t kDefaultBufferSize = std::size_t{64} * 1024;

    /// @brief IO status, result, and option types.
    namespace Types
    {
        /// @brief Portable error categories shared by IO and resource-owning backend libraries.
        /// @note Enumerator numeric values are not serialization IDs or stable wire-format values.
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
            /// @brief Moving or renaming a resource failed.
            MoveFailed,
            /// @brief Resizing a resource failed.
            ResizeFailed,
            /// @brief Acquiring a resource lock failed.
            LockFailed,
            /// @brief Releasing a resource lock failed.
            UnlockFailed,
            /// @brief Creating a directory failed.
            DirectoryCreateFailed,
            /// @brief Listing a directory failed.
            DirectoryListFailed,
            /// @brief A directory could not be removed because it is not empty.
            DirectoryNotEmpty,

            /// @brief A read ended before the promised byte count was produced.
            PartialRead,
            /// @brief A write accepted fewer bytes than required before it failed.
            PartialWrite,
            /// @brief A requested, known, or observed size exceeds the accepted limit.
            SizeLimitExceeded,
            /// @brief A required memory allocation failed.
            OutOfMemory,

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
            [[nodiscard]] constexpr bool ok() const noexcept
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
            /// @brief Operation status. A failure may accompany valid bytes reported by bytesRead.
            Status status;
            /// @brief Number of valid bytes copied into the caller destination.
            /// @note Must not exceed the destination span supplied to Reader::read().
            std::size_t bytesRead = 0;
            /// @brief True when no additional input remains after this read.
            /// @note This is independent of bytesRead and may be true on a final non-empty read.
            bool endOfStream = false;
        };

        /// @brief Result returned by byte write operations.
        struct WriteResult
        {
            /// @brief Operation status. A failure may accompany valid bytes reported by bytesWritten.
            Status status;
            /// @brief Number of bytes accepted from the caller input.
            /// @note Must not exceed the input span supplied to Writer::write().
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

    /// @brief Returns whether a flush mode is one of the defined FlushMode values.
    /// @param mode Flush mode to validate.
    /// @return True for None, Data, and DataAndMetadataBestEffort; otherwise false.
    [[nodiscard]] constexpr bool isValidFlushMode(Types::FlushMode mode) noexcept
    {
        switch (mode)
        {
        case Types::FlushMode::None:
        case Types::FlushMode::Data:
        case Types::FlushMode::DataAndMetadataBestEffort:
            return true;
        }

        return false;
    }

    /// @brief Creates an IO status with portable, native, and diagnostic details.
    /// @param code Portable status code used for program decisions.
    /// @param nativeCode Backend-native error code, or zero when unavailable.
    /// @param message Developer-facing diagnostic text; not stable for machine parsing.
    /// @return Status containing the supplied values.
    /// @note The function is non-throwing, but constructing the by-value message argument occurs
    /// before function entry and may allocate. Code-only calls avoid that allocation.
    [[nodiscard]] Types::Status makeStatus(Types::ErrorCode code, std::int64_t nativeCode = 0, std::string message = {}) noexcept;

    /// @brief Creates a successful IO status.
    /// @return Status whose code is ErrorCode::Success.
    [[nodiscard]] Types::Status successStatus() noexcept;

    /// @brief Returns the symbolic name of an ErrorCode value.
    /// @param code Error code to name.
    /// @return Stable non-owning string literal for known values, or "Unknown" for unknown enumerators.
    /// @note Use the name for diagnostics and tests, not as a substitute for an application-owned
    /// serialized or wire-format error representation.
    [[nodiscard]] std::string_view errorCodeName(Types::ErrorCode code) noexcept;

    /// @brief Movable, non-copyable byte-input interface used by generic IO helpers.
    /// @details A concrete reader must implement read(). The base class models a stateless open
    /// stream: close() succeeds, canSeek() is false, and position(), size(), and seek() return
    /// NotSeekable. Backends override only the lifecycle and capabilities they support.
    ///
    /// read() reports copied bytes and end-of-stream independently. A final non-empty read may set
    /// endOfStream. A failure may report valid partial progress. Implementations must not retain the
    /// caller destination span after return unless a separate public contract extends its lifetime.
    ///
    /// Expected I/O failures use status values. The virtual functions are not globally noexcept, so
    /// exceptions from a custom implementation may propagate through callers and generic helpers.
    ///
    /// Different Reader objects may be used concurrently. The same object is not thread-safe unless
    /// its concrete implementation explicitly documents otherwise.
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
        /// @note This is advisory. Callers must still inspect position(), size(), and seek() results.
        [[nodiscard]] virtual bool canSeek() const noexcept;

        /// @brief Reads bytes into caller-owned memory.
        /// @param destination Destination memory valid for the duration of the call.
        /// @return Read status, byte count, and end-of-stream state.
        /// @note bytesRead must never exceed destination.size(). A successful zero-byte result is
        /// valid only for an empty request or when endOfStream is true.
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

    /// @brief Movable, non-copyable byte-output interface used by generic IO helpers.
    /// @details A concrete writer must implement write(). The base class models a stateless open
    /// stream: close() succeeds, canSeek() is false, position() and seek() return NotSeekable, and
    /// flush() validates the mode before succeeding without requiring physical I/O.
    ///
    /// write() reports accepted bytes independently from failure status. A failure may report valid
    /// partial progress. Implementations must not retain the caller input span after return unless a
    /// separate public contract extends its lifetime. Successful zero progress for non-empty input
    /// violates the retry contract used by writeAllBytes().
    ///
    /// Expected I/O failures use status values. The virtual functions are not globally noexcept, so
    /// exceptions from a custom implementation may propagate through callers and generic helpers.
    ///
    /// Different Writer objects may be used concurrently. The same object is not thread-safe unless
    /// its concrete implementation explicitly documents otherwise.
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
        /// @note This is advisory. Callers must still inspect position() and seek() results.
        [[nodiscard]] virtual bool canSeek() const noexcept;

        /// @brief Writes bytes from caller-owned memory.
        /// @param bytes Input memory valid for the duration of the call.
        /// @return Write status and accepted-byte count.
        /// @note bytesWritten must never exceed bytes.size(). Empty input succeeds with zero bytes;
        /// non-empty input must not return successful zero progress.
        [[nodiscard]] virtual Types::WriteResult write(std::span<const std::byte> bytes) = 0;

        /// @brief Flushes buffered data when the writer owns flushable state.
        /// @param mode Requested flush strength.
        /// @return InvalidArgument for an unknown mode; otherwise success by default.
        /// @note The base implementation performs no physical I/O.
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

    /// @brief Non-owning, seekable Reader over existing contiguous byte storage.
    /// @details The source must remain alive and at a stable address while this reader is used.
    /// Direct temporary std::string and std::vector construction is rejected, but caller-created
    /// dangling spans and string views cannot be detected. Closing the reader never modifies the
    /// source and cannot be reversed.
    /// @note Reads are overlap-safe. MemoryReader allocates nothing, performs no operating-system
    /// calls, and is not internally synchronized.
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
        /// @param destination Destination memory valid for the duration of the call.
        /// @return NotOpen when closed; otherwise success, copied-byte count, and end-of-stream state.
        /// @note Destination may overlap the source memory. An empty destination reports whether the
        /// current position is already at end-of-stream.
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

    /// @brief Owning, append-only Writer backed by std::vector<std::byte>.
    /// @details Collected output remains inspectable, clearable, reservable, and extractable after
    /// close(), while write(), flush(), and position() require open state. Writes from a valid
    /// subspan of the writer's current bytes() view are supported even when appending reallocates.
    /// @note MemoryWriter is non-seekable, not internally synchronized, and performs no
    /// operating-system calls.
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

        /// @brief Appends bytes, including valid subspans of this writer's current bytes() view.
        /// @param bytes Bytes valid for the duration of the call.
        /// @return Success and the full byte count, or NotOpen, SizeLimitExceeded, OutOfMemory, or
        /// InvalidArgument with zero accepted bytes.
        /// @note Any previously returned bytes() view may be invalidated by the append.
        [[nodiscard]] Types::WriteResult write(std::span<const std::byte> bytes) override;

        /// @brief Validates open state; memory-backed writes require no physical flush.
        /// @param mode Requested flush strength.
        /// @return Success while open, InvalidArgument for an unknown mode, or NotOpen after close().
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
        /// @note This operation is available after close() and may invalidate a bytes() view.
        void reserve(std::size_t capacity);

        /// @brief Returns a read-only view of the bytes written so far.
        /// @return Non-owning view into this writer's current storage.
        /// @warning Do not retain the view across write(), reserve(), clear(), takeBytes(), move,
        /// destruction, or any operation that may change storage or ownership.
        [[nodiscard]] std::span<const std::byte> bytes() const noexcept;

        /// @brief Returns collected bytes interpreted as a string of UTF-8 bytes.
        /// @return Copy of collected bytes as std::string. No UTF-8 validation is performed.
        /// @throws std::bad_alloc When the returned string cannot be allocated.
        /// @throws std::length_error When the collected byte count exceeds the string maximum size.
        [[nodiscard]] std::string text() const;

        /// @brief Moves collected bytes out and replaces writer storage with an empty vector.
        /// @return Collected byte vector.
        /// @note The writer preserves its open or closed state. The operation invalidates prior
        /// bytes() views and may discard previously reserved capacity.
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
        /// @note This operation is available after close() and invalidates prior bytes() views.
        void clear() noexcept;

    private:
        std::vector<std::byte> bytes_;
        bool open_ = true;
    };

    /// @brief Reads from the current reader position until the known remainder, end-of-stream, or failure.
    /// @param reader Reader to drain.
    /// @param maxBytes Hard maximum accepted output size, or kNoByteLimit for no caller limit.
    /// @param bufferSize Temporary transfer-buffer size for unknown-size readers. Must be nonzero.
    /// @return Collected bytes and final status, preserving valid bytes produced before a later failure.
    /// @note Known-size readers allocate final output directly. An unknown-size reader at a finite
    /// limit may be advanced by one extra probe byte that is not stored.
    /// @note Invalid buffer arguments, capability failures, partial known-size reads, invalid backend
    /// progress, representation limits, and internal allocation failures are reported through status.
    /// Arbitrary exceptions from custom readers may propagate.
    [[nodiscard]] Types::ReadAllBytesResult readAllBytes(
        Reader &reader,
        std::uint64_t maxBytes = kNoByteLimit,
        std::size_t bufferSize = kDefaultBufferSize);

    /// @brief Reads all bytes using caller-owned temporary storage for unknown-size readers.
    /// @param reader Reader to drain from its current position.
    /// @param scratchBuffer Non-empty temporary storage that may be overwritten during the call.
    /// @param maxBytes Hard maximum accepted output size, or kNoByteLimit for no caller limit.
    /// @return Collected bytes and final status, preserving valid bytes produced before a later failure.
    /// @note The scratch buffer is ignored for known-size readers and is never retained. At a finite
    /// limit, an unknown-size reader may be advanced by one extra probe byte that is not stored.
    /// Arbitrary exceptions from custom readers may propagate.
    [[nodiscard]] Types::ReadAllBytesResult readAllBytes(Reader &reader, std::span<std::byte> scratchBuffer, std::uint64_t maxBytes = kNoByteLimit);

    /// @brief Reads bytes from the current reader position into an owning string.
    /// @param reader Reader to drain.
    /// @param maxBytes Hard maximum accepted output size, or kNoByteLimit for no caller limit.
    /// @param bufferSize Temporary transfer-buffer size for unknown-size readers. Must be nonzero.
    /// @return Collected text bytes and final status, preserving valid bytes produced before a later failure.
    /// @note Bytes are copied without UTF-8 validation, normalization, or parsing. Known-size readers
    /// allocate final output directly. An unknown-size reader at a finite limit may consume one
    /// additional probe byte. Arbitrary exceptions from custom readers may propagate.
    [[nodiscard]] Types::ReadAllTextResult readAllText(
        Reader &reader,
        std::uint64_t maxBytes = kNoByteLimit,
        std::size_t bufferSize = kDefaultBufferSize);

    /// @brief Reads all text bytes using caller-owned temporary storage for unknown-size readers.
    /// @param reader Reader to drain from its current position.
    /// @param scratchBuffer Non-empty temporary storage that may be overwritten during the call.
    /// @param maxBytes Hard maximum accepted output size, or kNoByteLimit for no caller limit.
    /// @return Collected text bytes and final status, preserving valid bytes produced before a later failure.
    /// @note The scratch buffer is ignored for known-size readers and is never retained. Bytes are not
    /// validated as UTF-8. A finite-limit probe may consume one additional unstored byte. Arbitrary
    /// exceptions from custom readers may propagate.
    [[nodiscard]] Types::ReadAllTextResult readAllText(Reader &reader, std::span<std::byte> scratchBuffer, std::uint64_t maxBytes = kNoByteLimit);

    /// @brief Writes all bytes, retrying successful short writes until complete or failed.
    /// @param writer Writer that receives the bytes.
    /// @param bytes Bytes valid for the duration of the call.
    /// @return Final status and total accepted bytes, including progress from a final failing write.
    /// @note Empty input succeeds without calling writer.write(). The helper does not flush or close
    /// the writer. Impossible counts and successful zero progress return WriteFailed. Arbitrary
    /// exceptions from a custom writer may propagate.
    [[nodiscard]] Types::WriteResult writeAllBytes(Writer &writer, std::span<const std::byte> bytes);

    /// @brief Writes all vector bytes, retrying successful short writes until complete or failed.
    /// @tparam Allocator Vector allocator type.
    /// @param writer Writer to drain bytes into.
    /// @param bytes Bytes to write.
    /// @return Final status and the total number of bytes accepted, including bytes accepted by a failing write.
    template <typename Allocator> [[nodiscard]] Types::WriteResult writeAllBytes(Writer &writer, const std::vector<std::byte, Allocator> &bytes)
    {
        return writeAllBytes(writer, std::span<const std::byte>(bytes.data(), bytes.size()));
    }

    /// @brief Writes every byte in a string view through writeAllBytes().
    /// @param writer Writer that receives the bytes.
    /// @param utf8Text Text byte view; embedded NUL bytes are preserved.
    /// @return Final status and total accepted bytes, including progress from a final failing write.
    /// @note No UTF-8 validation, normalization, parsing, flush, or close operation is performed.
    [[nodiscard]] Types::WriteResult writeAllText(Writer &writer, std::string_view utf8Text);
} // namespace GameWIP::IO
