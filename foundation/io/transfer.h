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

    /// @name Whole-stream transfer
    /// @{

    /// @brief Reads from the current reader position until the known remainder, end-of-stream, or failure.
    /// @param reader Reader starting at the current position.
    /// @param maxBytes Hard maximum accepted output size, or kNoByteLimit for no caller limit.
    /// @param bufferSize Temporary transfer-buffer size for unknown-size readers. Must be nonzero.
    /// @return Collected bytes, preserving available partial data on later failure.
    /// @note Known-size readers allocate final output directly. An unknown-size reader at a finite
    /// limit may be advanced by one extra probe byte that is not stored.
    /// @note Invalid buffer arguments, capability failures, partial known-size reads, invalid backend
    /// progress, representation limits, and internal allocation failures are reported through status.
    [[nodiscard]] GameWIP::Status::Types::Result<std::vector<std::byte>> readAllBytes(
        Reader &reader,
        std::uint64_t maxBytes = kNoByteLimit,
        std::size_t bufferSize = kDefaultBufferSize) noexcept;

    /// @brief Reads all bytes using caller-owned temporary storage for unknown-size readers.
    /// @param reader Reader to drain from its current position.
    /// @param scratchBuffer Non-empty temporary storage that may be overwritten during the call.
    /// @param maxBytes Hard maximum accepted output size, or kNoByteLimit for no caller limit.
    /// @return Collected bytes, preserving available partial data on later failure.
    /// @note The scratch buffer is ignored for known-size readers and is never retained. At a finite
    /// limit, an unknown-size reader may be advanced by one extra probe byte that is not stored.
    [[nodiscard]] GameWIP::Status::Types::Result<std::vector<std::byte>> readAllBytes(
        Reader &reader,
        std::span<std::byte> scratchBuffer,
        std::uint64_t maxBytes = kNoByteLimit) noexcept;

    /// @brief Reads strict UTF-8 text from the current reader position into an owning string.
    /// @param reader Reader starting at the current position.
    /// @param maxBytes Hard maximum accepted output size, or kNoByteLimit for no caller limit.
    /// @param bufferSize Temporary transfer-buffer size for unknown-size readers. Must be nonzero.
    /// @return Validated text; on failure, only the complete valid prefix when available.
    /// @see @ref io_utf8_policy
    [[nodiscard]] GameWIP::Status::Types::Result<std::string> readAllText(
        Reader &reader,
        std::uint64_t maxBytes = kNoByteLimit,
        std::size_t bufferSize = kDefaultBufferSize) noexcept;

    /// @brief Reads strict UTF-8 text using caller-owned temporary storage for unknown-size readers.
    /// @param reader Reader to drain from its current position.
    /// @param scratchBuffer Non-empty temporary storage that may be overwritten during the call.
    /// @param maxBytes Hard maximum accepted output size, or kNoByteLimit for no caller limit.
    /// @return Validated text; on failure, only the complete valid prefix when available.
    /// @note The scratch buffer is ignored for known-size readers and is never retained.
    /// @see @ref io_utf8_policy
    [[nodiscard]] GameWIP::Status::Types::Result<std::string> readAllText(
        Reader &reader,
        std::span<std::byte> scratchBuffer,
        std::uint64_t maxBytes = kNoByteLimit) noexcept;

    /// @brief Writes all bytes, retrying successful short writes until complete or failed.
    /// @param writer Writer receiving the bytes.
    /// @param bytes Bytes valid for the duration of the call.
    /// @return Total accepted bytes, including progress from a final failing write.
    /// @note Empty input succeeds without calling writer.write(). The helper does not flush or close
    /// the writer. Impossible counts and successful zero progress return WriteFailed.
    [[nodiscard]] GameWIP::Status::Types::Result<Types::WriteProgress> writeAllBytes(Writer &writer, std::span<const std::byte> bytes) noexcept;

    /// @brief Writes all vector bytes, retrying successful short writes until complete or failed.
    /// @tparam Allocator Vector allocator type.
    /// @return Total accepted bytes, including bytes accepted by a failing write.
    template <typename Allocator>
    [[nodiscard]] GameWIP::Status::Types::Result<Types::WriteProgress> writeAllBytes(
        Writer &writer,
        const std::vector<std::byte, Allocator> &bytes) noexcept
    {
        return writeAllBytes(writer, std::span<const std::byte>(bytes.data(), bytes.size()));
    }

    /// @brief Writes complete strict UTF-8 text through writeAllBytes().
    /// @param writer Writer receiving the bytes.
    /// @param utf8Text Text to validate and write.
    /// @return EncodingFailed with zero bytes written for invalid input; otherwise the writeAllBytes() result.
    /// @note Validation happens before writer.write() is called. No flush or close is performed.
    /// @see @ref io_utf8_policy
    [[nodiscard]] GameWIP::Status::Types::Result<Types::WriteProgress> writeAllText(Writer &writer, std::string_view utf8Text) noexcept;

    /// @}
} // namespace GameWIP::IO
