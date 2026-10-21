/// @file stream.h
/// @brief Public reader, writer, and stream result contracts for GameWIP IO.

#pragma once

#include "io/status.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace GameWIP::IO
{
    namespace Types
    {
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
            /// @brief Valid bytes copied; never exceeds the destination size.
            std::size_t bytesRead = 0;
            /// @brief True when no additional input remains; independent of bytesRead.
            bool endOfStream = false;
        };

        /// @brief Result returned by byte write operations.
        struct WriteResult
        {
            /// @brief Operation status. A failure may accompany valid bytes reported by bytesWritten.
            Status status;
            /// @brief Bytes accepted; never exceeds the input size.
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
    } // namespace Types

    /// @brief Returns whether a flush mode is one of the defined FlushMode values.
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

    /// @brief Movable, non-copyable byte-input interface; implement read() and override only supported capabilities.
    /// @details Base defaults: stateless open stream, close() succeeds, canSeek() is false, position()/size()/seek()
    /// return NotSeekable. Implementations must not retain the destination span and must contain failures in statuses;
    /// throwing from an override violates the contract.
    /// @see io_reader_writer_contract
    class Reader
    {
    public:
        Reader() = default;

        virtual ~Reader() noexcept = default;

        Reader(const Reader &) = delete;

        Reader &operator=(const Reader &) = delete;

        /// @brief Moves the base state; concrete readers define any native-handle transfer.
        Reader(Reader &&) noexcept = default;

        /// @brief Moves the base state; concrete readers define any native-handle transfer.
        Reader &operator=(Reader &&) noexcept = default;

        /// @brief Returns whether this reader has readable state.
        /// @return True for stateless readers by default.
        [[nodiscard]] virtual bool isOpen() const noexcept;

        /// @brief Returns whether this reader supports seek operations.
        /// @return False by default.
        /// @note This is advisory. Callers must still inspect position(), size(), and seek() results.
        [[nodiscard]] virtual bool canSeek() const noexcept;

        /// @brief Reads bytes into the destination.
        /// @param destination Destination valid for the call.
        /// @return Read status, byte count, and end-of-stream state.
        /// @note A successful zero-byte result is valid only for an empty request or at end of stream.
        [[nodiscard]] virtual Types::ReadResult read(std::span<std::byte> destination) noexcept = 0;

        /// @brief Closes the reader when it owns closeable state.
        /// @return Success or a close failure status.
        [[nodiscard]] virtual Types::Status close() noexcept;

        /// @brief Returns the current stream position when supported.
        /// @return Current position, or NotSeekable for non-position-aware readers.
        [[nodiscard]] virtual Types::PositionResult position() const noexcept;

        /// @brief Returns the stream size when supported.
        /// @return Stream size, or NotSeekable for non-size-aware readers.
        [[nodiscard]] virtual Types::SizeResult size() const noexcept;

        /// @brief Moves the stream position when supported.
        /// @param offset Signed offset relative to origin.
        /// @param origin Origin used to interpret offset.
        /// @return Success or a seek-related failure status.
        [[nodiscard]] virtual Types::Status seek(std::int64_t offset, Types::SeekOrigin origin) noexcept;
    };

    /// @brief Movable, non-copyable byte-output interface; implement write() and override only supported capabilities.
    /// @details Base defaults: stateless open stream, close() succeeds, canSeek() is false, position()/seek() return
    /// NotSeekable, flush() validates the mode only. Implementations must not retain the input span and must contain
    /// failures in statuses; throwing from an override violates the contract.
    /// @see io_reader_writer_contract
    class Writer
    {
    public:
        Writer() = default;

        virtual ~Writer() noexcept = default;

        Writer(const Writer &) = delete;

        Writer &operator=(const Writer &) = delete;

        /// @brief Moves the base state; concrete writers define any native-handle transfer.
        Writer(Writer &&) noexcept = default;

        /// @brief Moves the base state; concrete writers define any native-handle transfer.
        Writer &operator=(Writer &&) noexcept = default;

        /// @brief Returns whether this writer has writable state.
        /// @return True for stateless writers by default.
        [[nodiscard]] virtual bool isOpen() const noexcept;

        /// @brief Returns whether this writer supports seek operations.
        /// @return False by default.
        /// @note This is advisory. Callers must still inspect position() and seek() results.
        [[nodiscard]] virtual bool canSeek() const noexcept;

        /// @brief Writes bytes.
        /// @param bytes Input valid for the call.
        /// @return Write status and accepted-byte count.
        /// @note Non-empty input must not return successful zero progress.
        [[nodiscard]] virtual Types::WriteResult write(std::span<const std::byte> bytes) noexcept = 0;

        /// @brief Flushes buffered data when the writer owns flushable state.
        /// @param mode Requested flush strength.
        /// @return InvalidArgument for an unknown mode; otherwise success by default.
        /// @note The base implementation performs no physical I/O.
        [[nodiscard]] virtual Types::Status flush(Types::FlushMode mode = Types::FlushMode::Data) noexcept;

        /// @brief Closes the writer when it owns closeable state.
        /// @return Success or a close failure status.
        [[nodiscard]] virtual Types::Status close() noexcept;

        /// @brief Returns the current stream position when supported.
        /// @return Current position, or NotSeekable for non-position-aware writers.
        [[nodiscard]] virtual Types::PositionResult position() const noexcept;

        /// @brief Moves the stream position when supported.
        /// @param offset Signed offset relative to origin.
        /// @param origin Origin used to interpret offset.
        /// @return Success or a seek-related failure status.
        [[nodiscard]] virtual Types::Status seek(std::int64_t offset, Types::SeekOrigin origin) noexcept;
    };
} // namespace GameWIP::IO
