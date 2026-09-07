/// @file transfer.h
/// @brief Public whole-stream transfer helpers for GameWIP IO.

#pragma once

#include "io/stream.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace GameWIP::IO
{
    /// @brief Sentinel meaning a whole-stream read has no caller-imposed byte limit.
    /// @note Result-container, address-space, and allocation limits still apply.
    inline constexpr std::uint64_t kNoByteLimit = std::numeric_limits<std::uint64_t>::max();

    /// @brief Default temporary buffer size for internally buffered unknown-size reads.
    /// @note Known-size reads allocate their final output directly and do not use this buffer size.
    inline constexpr std::size_t kDefaultBufferSize = std::size_t{64} * 1024;

    namespace Types
    {
        /// @brief Result returned by whole-stream byte reads.
        struct ReadAllBytesResult
        {
            /// @brief Operation status.
            Status status;
            /// @brief Bytes collected before end-of-stream or failure.
            std::vector<std::byte> bytes;
        };

        /// @brief Result returned by whole-stream UTF-8 text reads.
        struct ReadAllTextResult
        {
            /// @brief Operation status.
            Status status;
            /// @brief Valid UTF-8 text collected before end-of-stream or failure.
            /// @note On encoding failure or an incomplete suffix, this contains only the complete valid UTF-8 prefix.
            std::string text;
        };
    } // namespace Types

    /// @name Whole-stream transfer
    /// @{

    /// @brief Reads from the current reader position until the known remainder, end-of-stream, or failure.
    /// @param reader Reader to drain.
    /// @param maxBytes Hard maximum accepted output size, or kNoByteLimit for no caller limit.
    /// @param bufferSize Temporary transfer-buffer size for unknown-size readers. Must be nonzero.
    /// @return Collected bytes and final status, preserving valid bytes produced before a later failure.
    /// @note Known-size readers allocate final output directly. An unknown-size reader at a finite
    /// limit may be advanced by one extra probe byte that is not stored.
    /// @note Invalid buffer arguments, capability failures, partial known-size reads, invalid backend
    /// progress, representation limits, and internal allocation failures are reported through status.
    [[nodiscard]] Types::ReadAllBytesResult readAllBytes(
        Reader &reader,
        std::uint64_t maxBytes = kNoByteLimit,
        std::size_t bufferSize = kDefaultBufferSize) noexcept;

    /// @brief Reads all bytes using caller-owned temporary storage for unknown-size readers.
    /// @param reader Reader to drain from its current position.
    /// @param scratchBuffer Non-empty temporary storage that may be overwritten during the call.
    /// @param maxBytes Hard maximum accepted output size, or kNoByteLimit for no caller limit.
    /// @return Collected bytes and final status, preserving valid bytes produced before a later failure.
    /// @note The scratch buffer is ignored for known-size readers and is never retained. At a finite
    /// limit, an unknown-size reader may be advanced by one extra probe byte that is not stored.
    [[nodiscard]] Types::ReadAllBytesResult readAllBytes(
        Reader &reader,
        std::span<std::byte> scratchBuffer,
        std::uint64_t maxBytes = kNoByteLimit) noexcept;

    /// @brief Reads strict UTF-8 text from the current reader position into an owning string.
    /// @param reader Reader to drain.
    /// @param maxBytes Hard maximum accepted output size, or kNoByteLimit for no caller limit.
    /// @param bufferSize Temporary transfer-buffer size for unknown-size readers. Must be nonzero.
    /// @return Valid UTF-8 text and final status, preserving only the complete valid UTF-8 prefix on failure.
    /// @note Malformed input returns EncodingFailed. An incomplete suffix returns EncodingFailed when the
    /// input reaches a definitive end; otherwise it is trimmed while an existing I/O or size-limit failure
    /// remains primary. No normalization, BOM transformation, or parsing is performed.
    [[nodiscard]] Types::ReadAllTextResult readAllText(
        Reader &reader,
        std::uint64_t maxBytes = kNoByteLimit,
        std::size_t bufferSize = kDefaultBufferSize) noexcept;

    /// @brief Reads strict UTF-8 text using caller-owned temporary storage for unknown-size readers.
    /// @param reader Reader to drain from its current position.
    /// @param scratchBuffer Non-empty temporary storage that may be overwritten during the call.
    /// @param maxBytes Hard maximum accepted output size, or kNoByteLimit for no caller limit.
    /// @return Valid UTF-8 text and final status, preserving only the complete valid UTF-8 prefix on failure.
    /// @note The scratch buffer is ignored for known-size readers and is never retained. A finite-limit
    /// probe may consume one additional unstored byte. No normalization, BOM transformation, or parsing occurs.
    [[nodiscard]] Types::ReadAllTextResult readAllText(
        Reader &reader,
        std::span<std::byte> scratchBuffer,
        std::uint64_t maxBytes = kNoByteLimit) noexcept;

    /// @brief Writes all bytes, retrying successful short writes until complete or failed.
    /// @param writer Writer that receives the bytes.
    /// @param bytes Bytes valid for the duration of the call.
    /// @return Final status and total accepted bytes, including progress from a final failing write.
    /// @note Empty input succeeds without calling writer.write(). The helper does not flush or close
    /// the writer. Impossible counts and successful zero progress return WriteFailed.
    [[nodiscard]] Types::WriteResult writeAllBytes(Writer &writer, std::span<const std::byte> bytes) noexcept;

    /// @brief Writes all vector bytes, retrying successful short writes until complete or failed.
    /// @tparam Allocator Vector allocator type.
    /// @param writer Writer to drain bytes into.
    /// @param bytes Bytes to write.
    /// @return Final status and the total number of bytes accepted, including bytes accepted by a failing write.
    template <typename Allocator>
    [[nodiscard]] Types::WriteResult writeAllBytes(Writer &writer, const std::vector<std::byte, Allocator> &bytes) noexcept
    {
        return writeAllBytes(writer, std::span<const std::byte>(bytes.data(), bytes.size()));
    }

    /// @brief Writes complete strict UTF-8 text through writeAllBytes().
    /// @param writer Writer that receives the UTF-8 bytes.
    /// @param utf8Text UTF-8 text view; embedded NUL bytes are preserved.
    /// @return EncodingFailed with zero bytes written for malformed or incomplete input; otherwise
    /// the writeAllBytes() status and total accepted-byte progress.
    /// @note Validation happens before writer.write() is called. No normalization, BOM transformation,
    /// parsing, flush, or close operation is performed.
    [[nodiscard]] Types::WriteResult writeAllText(Writer &writer, std::string_view utf8Text) noexcept;

    /// @}
} // namespace GameWIP::IO
