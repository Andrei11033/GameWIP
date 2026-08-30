/// @file memory.h
/// @brief Public memory-backed stream contracts for GameWIP IO.

#pragma once

#include "io/stream.h"

#include <concepts>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace GameWIP::IO
{
    namespace Types
    {

        /// @brief Result returned when memory-writer bytes are copied as valid UTF-8 text.
        struct CopyTextResult
        {
            /// @brief Operation status.
            Status status;
            /// @brief Owning UTF-8 copy when status is successful; empty on EncodingFailed.
            std::string text;
        };
    } // namespace Types

    /// @name Memory streams
    /// @{

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

        /// @brief Creates a byte reader over caller-owned character storage.
        /// @param bytes Character-backed bytes to read. The caller must keep this string view storage alive while the reader is used.
        /// @note The constructor does not interpret or validate the byte encoding.
        explicit MemoryReader(std::string_view bytes) noexcept;

        /// @brief Rejects temporary std::string storage that would leave the reader dangling.
        /// @tparam String Exact std::string rvalue type.
        /// @param bytes Temporary character-backed byte storage.
        template <typename String>
            requires(std::same_as<std::remove_cvref_t<String>, std::string> && std::is_rvalue_reference_v<String &&>)
        explicit MemoryReader(String &&bytes) = delete;

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
        [[nodiscard]] Types::ReadResult read(std::span<std::byte> destination) noexcept override;

        /// @brief Closes this reader without affecting caller-owned source storage.
        /// @return Success. Repeated close calls also succeed.
        [[nodiscard]] Types::Status close() noexcept override;

        /// @brief Returns the current byte position while open.
        /// @return Current position, or NotOpen after close().
        [[nodiscard]] Types::PositionResult position() const noexcept override;

        /// @brief Returns the source byte count while open.
        /// @return Source size, or NotOpen after close().
        [[nodiscard]] Types::SizeResult size() const noexcept override;

        /// @brief Moves the current position within the source byte range.
        /// @param offset Signed offset relative to origin.
        /// @param origin Origin used to interpret offset.
        /// @return Success, NotOpen, InvalidArgument, SizeLimitExceeded, or SeekFailed.
        [[nodiscard]] Types::Status seek(std::int64_t offset, Types::SeekOrigin origin) noexcept override;

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
        MemoryWriter() noexcept = default;

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
        [[nodiscard]] Types::WriteResult write(std::span<const std::byte> bytes) noexcept override;

        /// @brief Validates open state; memory-backed writes require no physical flush.
        /// @param mode Requested flush strength.
        /// @return Success while open, InvalidArgument for an unknown mode, or NotOpen after close().
        [[nodiscard]] Types::Status flush(Types::FlushMode mode = Types::FlushMode::Data) noexcept override;

        /// @brief Closes this writer without discarding collected output.
        /// @return Success. Repeated close calls also succeed.
        [[nodiscard]] Types::Status close() noexcept override;

        /// @brief Returns the append position while open.
        /// @return Current byte count as the position, or NotOpen after close().
        [[nodiscard]] Types::PositionResult position() const noexcept override;

        /// @brief Writes bytes from vector storage.
        /// @tparam Allocator Vector allocator type.
        /// @param bytes Bytes to append.
        /// @return Write status and byte count.
        template <typename Allocator> [[nodiscard]] Types::WriteResult write(const std::vector<std::byte, Allocator> &bytes) noexcept
        {
            return write(std::span<const std::byte>(bytes.data(), bytes.size()));
        }

        /// @brief Reserves at least the requested byte capacity.
        /// @param capacity Byte capacity to reserve.
        /// @return Success, SizeLimitExceeded, OutOfMemory, or Unknown.
        /// @note This operation is available after close() and may invalidate a bytes() view.
        [[nodiscard]] Types::Status reserve(std::size_t capacity) noexcept;

        /// @brief Returns a read-only view of the bytes written so far.
        /// @return Non-owning view into this writer's current storage.
        /// @warning Do not retain the view across write(), reserve(), clear(), takeBytes(), move,
        /// destruction, or any operation that may change storage or ownership.
        [[nodiscard]] std::span<const std::byte> bytes() const noexcept;

        /// @brief Copies collected bytes into an owning UTF-8 string after strict validation.
        /// @return Copied text and success, or EncodingFailed, SizeLimitExceeded, OutOfMemory, or Unknown.
        /// @note EncodingFailed returns empty text. The operation is available after close() and preserves embedded NUL bytes.
        [[nodiscard]] Types::CopyTextResult copyText() const noexcept;

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

    /// @}
} // namespace GameWIP::IO
