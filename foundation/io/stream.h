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

    /// @brief Movable, non-copyable byte-input interface used by generic IO helpers.
    /// @details A concrete reader must implement read(). The base class models a stateless open
    /// stream: close() succeeds, canSeek() is false, and position(), size(), and seek() return
    /// NotSeekable. Backends override only the lifecycle and capabilities they support.
    ///
    /// read() reports copied bytes and end-of-stream independently. A final non-empty read may set
    /// endOfStream. A failure may report valid partial progress. Implementations must not retain the
    /// caller destination span after return unless a separate public contract extends its lifetime.
    ///
    /// Expected I/O, allocation, length, platform, and implementation failures must be translated
    /// into status values by concrete implementations. Throwing from an override violates the
    /// extension contract.
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
    /// Expected I/O, allocation, length, platform, and implementation failures must be translated
    /// into status values by concrete implementations. Throwing from an override violates the
    /// extension contract.
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
