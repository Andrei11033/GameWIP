/// @file unicode.h
/// @brief Public API for the Unicode foundation library.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

/// @brief Platform-neutral Unicode encoding, validation, conversion, and text-boundary primitives.
/// @details GameWIP text uses UTF-8 as its canonical public representation. The library provides strict,
/// non-allocating Unicode primitives and a portable UTF-16 bridge without defining recovery, editing,
/// rendering, locale, normalization, or terminal-cell-width policy. Public operations are noexcept, use
/// immutable generated data, perform no implementation-owned dynamic allocation, and keep no mutable
/// process-wide or thread-local last-error state.
namespace GameWIP::Unicode
{
    /// @brief Highest valid Unicode scalar value.
    inline constexpr char32_t kMaximumScalarValue = static_cast<char32_t>(0x10FFFF);

    /// @brief UTF-8 encoding operations and constants.
    namespace Utf8
    {
        /// @brief Maximum number of bytes required to encode one Unicode scalar as UTF-8.
        inline constexpr std::size_t kMaximumScalarBytes = 4;
    } // namespace Utf8

    /// @brief UTF-16 encoding operations and constants.
    namespace Utf16
    {
        /// @brief Maximum number of UTF-16 code units required to encode one Unicode scalar.
        inline constexpr std::size_t kMaximumScalarCodeUnits = 2;
    } // namespace Utf16

    /// @brief Passive Unicode values, shared outcomes, and encoding-specific result families.
    namespace Types
    {
        /// @brief Outcome of decoding one encoded Unicode scalar.
        enum class DecodeOutcome : std::uint8_t
        {
            /// @brief One complete Unicode scalar was decoded.
            Decoded,

            /// @brief The supplied input is a valid encoded prefix but requires additional input.
            Incomplete,

            /// @brief The supplied input is malformed and cannot become valid by appending input.
            InvalidEncoding
        };

        /// @brief Outcome of encoding one Unicode scalar.
        enum class EncodeOutcome : std::uint8_t
        {
            /// @brief The scalar was encoded successfully.
            Encoded,

            /// @brief The supplied value is not a valid Unicode scalar.
            InvalidScalar
        };

        /// @brief Outcome of validating an encoded text range.
        enum class ValidationOutcome : std::uint8_t
        {
            /// @brief The complete range is valid.
            Valid,

            /// @brief The valid prefix is followed by an incomplete final sequence.
            Incomplete,

            /// @brief The valid prefix is followed by malformed input.
            InvalidEncoding
        };

        /// @brief Outcome of measuring an encoding conversion.
        enum class MeasureOutcome : std::uint8_t
        {
            /// @brief The complete source range was measured successfully.
            Measured,

            /// @brief The valid source prefix is followed by an incomplete final sequence.
            Incomplete,

            /// @brief The valid source prefix is followed by malformed input.
            InvalidEncoding,

            /// @brief The required destination size cannot be represented by std::size_t.
            SizeLimitExceeded
        };

        /// @brief Outcome of converting between Unicode encodings.
        enum class ConversionOutcome : std::uint8_t
        {
            /// @brief The complete source range was converted successfully.
            Converted,

            /// @brief The converted source prefix is followed by an incomplete final sequence.
            Incomplete,

            /// @brief The converted source prefix is followed by malformed input.
            InvalidEncoding,

            /// @brief The destination cannot hold the next complete encoded scalar.
            DestinationTooSmall,

            /// @brief The source and destination memory ranges overlap.
            OverlappingRanges
        };

        /// @brief Unicode Standard version used by generated property data and grapheme segmentation.
        struct Version
        {
            /// @brief Major Unicode Standard version.
            std::uint8_t major = 0;

            /// @brief Minor Unicode Standard version.
            std::uint8_t minor = 0;

            /// @brief Patch Unicode Standard version.
            std::uint8_t patch = 0;
        };

        /// @brief Passive UTF-8 outcomes and results.
        namespace Utf8
        {
            /// @brief Outcome of traversing UTF-8 text boundaries.
            enum class BoundaryOutcome : std::uint8_t
            {
                /// @brief The requested boundary was found.
                Found,

                /// @brief Backward traversal was already at the beginning of the range.
                AtBeginning,

                /// @brief Forward traversal was already at the end of the range.
                AtEnd,

                /// @brief The supplied byte offset is outside the range or not code-point aligned.
                InvalidOffset,

                /// @brief Malformed or incomplete UTF-8 prevented traversal.
                InvalidEncoding
            };

            /// @brief Outcome of indexing UTF-8 grapheme boundaries for repeated traversal.
            enum class GraphemeIndexOutcome : std::uint8_t
            {
                /// @brief The complete valid range was indexed successfully.
                Indexed,

                /// @brief Caller-provided boundary storage cannot hold the complete index.
                DestinationTooSmall,

                /// @brief Malformed or incomplete UTF-8 prevented complete indexing.
                InvalidEncoding
            };

            /// @brief Result of decoding one UTF-8 scalar.
            struct DecodeResult
            {
                /// @brief Decoded scalar when outcome is Decoded.
                char32_t scalar = U'\0';

                /// @brief Number of UTF-8 bytes forming scalar when outcome is Decoded.
                std::uint8_t bytesConsumed = 0;

                /// @brief Classification of the decode attempt.
                DecodeOutcome outcome = DecodeOutcome::Incomplete;
            };

            /// @brief Fixed-size UTF-8 encoding of one Unicode scalar.
            struct EncodeResult
            {
                /// @brief Encoded UTF-8 bytes when outcome is Encoded.
                std::array<char, ::GameWIP::Unicode::Utf8::kMaximumScalarBytes> bytes{};

                /// @brief Number of valid elements stored in bytes.
                std::uint8_t byteCount = 0;

                /// @brief Classification of the encode attempt.
                EncodeOutcome outcome = EncodeOutcome::InvalidScalar;
            };

            /// @brief Result of validating an entire UTF-8 range.
            struct ValidationResult
            {
                /// @brief Number of leading bytes forming complete valid UTF-8 sequences.
                std::size_t validPrefixBytes = 0;

                /// @brief Classification of the complete range.
                ValidationOutcome outcome = ValidationOutcome::Valid;
            };

            /// @brief Result of measuring a UTF-8 to UTF-16 conversion.
            struct ToUtf16MeasureResult
            {
                /// @brief Number of source bytes included in the measured valid prefix.
                std::size_t sourceBytesProcessed = 0;

                /// @brief UTF-16 code units required to store the measured valid prefix.
                std::size_t requiredCodeUnits = 0;

                /// @brief Classification of the measurement attempt.
                MeasureOutcome outcome = MeasureOutcome::Measured;
            };

            /// @brief Result of converting UTF-8 to UTF-16.
            struct ToUtf16Result
            {
                /// @brief Number of complete source bytes consumed.
                std::size_t sourceBytesConsumed = 0;

                /// @brief Number of UTF-16 code units written.
                std::size_t codeUnitsWritten = 0;

                /// @brief Classification of the conversion attempt.
                ConversionOutcome outcome = ConversionOutcome::Converted;
            };

            /// @brief Result of traversing a UTF-8 code-point or grapheme boundary.
            struct BoundaryResult
            {
                /// @brief Resulting byte offset.
                /// @details On Found, this is the discovered boundary. On AtBeginning or AtEnd, this is the
                /// unchanged endpoint. On failure, this is the original caller-provided offset.
                std::size_t byteOffset = 0;

                /// @brief Classification of the traversal attempt.
                BoundaryOutcome outcome = BoundaryOutcome::InvalidOffset;
            };

            /// @brief Result of building caller-owned UTF-8 grapheme-boundary traversal state.
            struct GraphemeIndexResult
            {
                /// @brief Number of boundary offsets required for the complete valid text.
                /// @details The count includes byte offset 0 and the final text-size boundary. Empty text
                /// therefore requires one entry. The value is meaningful for Indexed and DestinationTooSmall.
                std::size_t requiredBoundaryCount = 0;

                /// @brief Classification of the indexing attempt.
                GraphemeIndexOutcome outcome = GraphemeIndexOutcome::Indexed;
            };
        } // namespace Utf8

        /// @brief Passive UTF-16 results.
        namespace Utf16
        {
            /// @brief Result of decoding one UTF-16 scalar.
            struct DecodeResult
            {
                /// @brief Decoded scalar when outcome is Decoded.
                char32_t scalar = U'\0';

                /// @brief Number of UTF-16 code units forming scalar when outcome is Decoded.
                std::uint8_t codeUnitsConsumed = 0;

                /// @brief Classification of the decode attempt.
                DecodeOutcome outcome = DecodeOutcome::Incomplete;
            };

            /// @brief Fixed-size UTF-16 encoding of one Unicode scalar.
            struct EncodeResult
            {
                /// @brief Encoded UTF-16 code units when outcome is Encoded.
                std::array<char16_t, ::GameWIP::Unicode::Utf16::kMaximumScalarCodeUnits> codeUnits{};

                /// @brief Number of valid elements stored in codeUnits.
                std::uint8_t codeUnitCount = 0;

                /// @brief Classification of the encode attempt.
                EncodeOutcome outcome = EncodeOutcome::InvalidScalar;
            };

            /// @brief Result of validating an entire UTF-16 range.
            struct ValidationResult
            {
                /// @brief Number of leading code units forming complete valid UTF-16 sequences.
                std::size_t validPrefixCodeUnits = 0;

                /// @brief Classification of the complete range.
                ValidationOutcome outcome = ValidationOutcome::Valid;
            };

            /// @brief Result of measuring a UTF-16 to UTF-8 conversion.
            struct ToUtf8MeasureResult
            {
                /// @brief Number of source code units included in the measured valid prefix.
                std::size_t sourceCodeUnitsProcessed = 0;

                /// @brief UTF-8 bytes required to store the measured valid prefix.
                std::size_t requiredBytes = 0;

                /// @brief Classification of the measurement attempt.
                MeasureOutcome outcome = MeasureOutcome::Measured;
            };

            /// @brief Result of converting UTF-16 to UTF-8.
            struct ToUtf8Result
            {
                /// @brief Number of complete source code units consumed.
                std::size_t sourceCodeUnitsConsumed = 0;

                /// @brief Number of UTF-8 bytes written.
                std::size_t bytesWritten = 0;

                /// @brief Classification of the conversion attempt.
                ConversionOutcome outcome = ConversionOutcome::Converted;
            };
        } // namespace Utf16
    } // namespace Types

    // ------------------------------------------------------------
    // Scalar and version queries
    // ------------------------------------------------------------

    /// @name Scalar and version queries
    /// @{

    /// @brief Returns whether a value is a Unicode scalar value.
    /// @param value Value to inspect.
    /// @return True when value is no greater than U+10FFFF and is not a surrogate code point.
    [[nodiscard]] constexpr bool isScalarValue(char32_t value) noexcept
    {
        constexpr char32_t firstSurrogate = static_cast<char32_t>(0xD800);
        constexpr char32_t lastSurrogate = static_cast<char32_t>(0xDFFF);

        return value <= kMaximumScalarValue && (value < firstSurrogate || value > lastSurrogate);
    }

    /// @brief Returns the Unicode Standard version implemented by this library.
    /// @return Version of the generated Unicode data and segmentation behavior.
    [[nodiscard]] Types::Version getStandardVersion() noexcept;

    /// @}

    // ------------------------------------------------------------
    // UTF-8 operations
    // ------------------------------------------------------------

    /// @name UTF-8 operations
    /// @{

    /// @brief Strict UTF-8 encoding, validation, conversion, and boundary operations.
    namespace Utf8
    {
        /// @brief Decodes one strict UTF-8 sequence from the beginning of a byte range.
        /// @param bytes Range beginning with the sequence to decode.
        /// @return Decoded, incomplete, or invalid result.
        /// @note Incomplete and invalid results return scalar U+0000 and consume zero bytes.
        [[nodiscard]] Types::Utf8::DecodeResult decodeScalar(std::string_view bytes) noexcept;

        /// @brief Encodes one Unicode scalar as UTF-8.
        /// @param scalar Value to encode.
        /// @return Fixed-size encoded result, or InvalidScalar.
        [[nodiscard]] Types::Utf8::EncodeResult encodeScalar(char32_t scalar) noexcept;

        /// @brief Validates an entire UTF-8 byte range.
        /// @param text UTF-8 bytes to validate.
        /// @return Validation outcome and complete valid-prefix length.
        /// @note An empty range and embedded U+0000 values are valid.
        [[nodiscard]] Types::Utf8::ValidationResult validate(std::string_view text) noexcept;

        /// @brief Measures the UTF-16 storage required for strict UTF-8 input.
        /// @param source UTF-8 source text.
        /// @return Measurement outcome, measured source prefix, and required UTF-16 code units.
        /// @note On source failure, progress describes only the complete valid prefix before the failing sequence.
        [[nodiscard]] Types::Utf8::ToUtf16MeasureResult measureToUtf16(std::string_view source) noexcept;

        /// @brief Converts strict UTF-8 input to caller-provided UTF-16 storage.
        /// @param source UTF-8 source text.
        /// @param destination Destination UTF-16 storage.
        /// @return Conversion outcome and completed source/output progress.
        /// @note No partial scalar or surrogate pair is written.
        /// @note No terminating U+0000 code unit is appended.
        /// @note Destination elements after codeUnitsWritten remain untouched.
        /// @note Completed progress is preserved before source failure or destination exhaustion.
        /// @note Overlapping source and destination memory is rejected before any output is written.
        [[nodiscard]] Types::Utf8::ToUtf16Result convertToUtf16(std::string_view source, std::span<char16_t> destination) noexcept;

        /// @brief Finds the next UTF-8 code-point boundary after a byte offset.
        /// @param text UTF-8 text to traverse.
        /// @param byteOffset Current byte offset.
        /// @return The next boundary or a deterministic endpoint/error result.
        /// @note byteOffset must identify a UTF-8 code-point boundary and may equal text.size().
        /// @note Malformed or incomplete UTF-8 required to establish the boundary produces InvalidEncoding.
        [[nodiscard]] Types::Utf8::BoundaryResult nextCodePointBoundary(std::string_view text, std::size_t byteOffset) noexcept;

        /// @brief Finds the previous UTF-8 code-point boundary before a byte offset.
        /// @param text UTF-8 text to traverse.
        /// @param byteOffset Current byte offset.
        /// @return The previous boundary or a deterministic endpoint/error result.
        /// @note byteOffset must identify a UTF-8 code-point boundary and may equal text.size().
        /// @note Malformed or incomplete UTF-8 required to establish the boundary produces InvalidEncoding.
        [[nodiscard]] Types::Utf8::BoundaryResult previousCodePointBoundary(std::string_view text, std::size_t byteOffset) noexcept;

        /// @brief Finds the next extended grapheme-cluster boundary after a byte offset.
        /// @param text UTF-8 text to traverse.
        /// @param byteOffset Current code-point-aligned byte offset.
        /// @return The next grapheme boundary or a deterministic endpoint/error result.
        /// @details When byteOffset lies inside an extended grapheme cluster, the returned boundary is
        /// the end of that containing cluster. Context-sensitive rules may require inspection of earlier
        /// text; malformed or incomplete UTF-8 encountered in required context produces InvalidEncoding.
        [[nodiscard]] Types::Utf8::BoundaryResult nextGraphemeBoundary(std::string_view text, std::size_t byteOffset) noexcept;

        /// @brief Finds the previous extended grapheme-cluster boundary before a byte offset.
        /// @param text UTF-8 text to traverse.
        /// @param byteOffset Current code-point-aligned byte offset.
        /// @return The previous grapheme boundary or a deterministic endpoint/error result.
        /// @details When byteOffset lies inside an extended grapheme cluster, the returned boundary is
        /// the beginning of that containing cluster. Context-sensitive rules may require inspection of
        /// earlier text; malformed or incomplete UTF-8 encountered in required context produces InvalidEncoding.
        [[nodiscard]] Types::Utf8::BoundaryResult previousGraphemeBoundary(std::string_view text, std::size_t byteOffset) noexcept;

        /// @brief Caller-backed cursor for efficient repeated UTF-8 grapheme-boundary traversal.
        /// @details reset() performs one complete segmentation pass and records every grapheme boundary
        /// in caller-provided storage. Once ready, next() and previous() are constant-time and seek() is
        /// logarithmic in the number of indexed boundaries. The cursor performs no implementation-owned
        /// allocation and does not retain the indexed text.
        class GraphemeCursor final
        {
        public:
            GraphemeCursor() noexcept = default;

            /// @brief Rebuilds the complete grapheme-boundary index and starts at byte offset 0.
            /// @param text UTF-8 text to segment.
            /// @param boundaryStorage Caller-owned offsets retained by the cursor after success.
            /// @return Required complete boundary count and indexing outcome.
            /// @details DestinationTooSmall still reports the complete required count. InvalidEncoding
            /// clears the cursor and reports a zero required count. Caller storage may contain an
            /// incomplete prefix after either failure.
            [[nodiscard]] Types::Utf8::GraphemeIndexResult reset(std::string_view text, std::span<std::size_t> boundaryStorage) noexcept;

            /// @brief Clears retained boundary storage and traversal position.
            void clear() noexcept;

            /// @brief Returns whether reset() completed successfully.
            [[nodiscard]] bool isReady() const noexcept;

            /// @brief Returns the current indexed grapheme-boundary byte offset, or 0 while not ready.
            [[nodiscard]] std::size_t byteOffset() const noexcept;

            /// @brief Returns the number of currently retained indexed boundaries, or 0 while not ready.
            [[nodiscard]] std::size_t boundaryCount() const noexcept;

            /// @brief Moves to one exact indexed grapheme boundary.
            /// @param byteOffset Boundary offset to locate.
            /// @return Found on success or InvalidOffset when not ready or the offset is not indexed.
            [[nodiscard]] Types::Utf8::BoundaryResult seek(std::size_t byteOffset) noexcept;

            /// @brief Advances to the next retained indexed grapheme boundary.
            /// @return Found with the new offset, AtEnd at the final boundary, or InvalidOffset while not ready.
            [[nodiscard]] Types::Utf8::BoundaryResult next() noexcept;

            /// @brief Moves to the previous retained indexed grapheme boundary.
            /// @return Found with the new offset, AtBeginning at offset 0, or InvalidOffset while not ready.
            [[nodiscard]] Types::Utf8::BoundaryResult previous() noexcept;

            /// @brief Discards every retained boundary after the current position.
            /// @details This is an O(1) index operation intended for callers that truncate or otherwise
            /// permanently discard the corresponding text suffix at the current indexed boundary.
            void discardAfterCurrent() noexcept;

        private:
            std::span<const std::size_t> boundaries_{};
            std::size_t currentBoundaryIndex_ = 0;
        };
    } // namespace Utf8

    /// @}

    // ------------------------------------------------------------
    // UTF-16 operations
    // ------------------------------------------------------------

    /// @name UTF-16 operations
    /// @{

    /// @brief Strict UTF-16 encoding, validation, and conversion operations.
    namespace Utf16
    {
        /// @brief Returns whether a UTF-16 code unit is a high surrogate.
        /// @param codeUnit Code unit to inspect.
        /// @return True for values in the range 0xD800 through 0xDBFF.
        [[nodiscard]] constexpr bool isHighSurrogate(char16_t codeUnit) noexcept
        {
            return codeUnit >= static_cast<char16_t>(0xD800) && codeUnit <= static_cast<char16_t>(0xDBFF);
        }

        /// @brief Returns whether a UTF-16 code unit is a low surrogate.
        /// @param codeUnit Code unit to inspect.
        /// @return True for values in the range 0xDC00 through 0xDFFF.
        [[nodiscard]] constexpr bool isLowSurrogate(char16_t codeUnit) noexcept
        {
            return codeUnit >= static_cast<char16_t>(0xDC00) && codeUnit <= static_cast<char16_t>(0xDFFF);
        }

        /// @brief Returns whether a UTF-16 code unit is any surrogate.
        /// @param codeUnit Code unit to inspect.
        /// @return True when codeUnit is a high or low surrogate.
        [[nodiscard]] constexpr bool isSurrogate(char16_t codeUnit) noexcept
        {
            return isHighSurrogate(codeUnit) || isLowSurrogate(codeUnit);
        }

        /// @brief Decodes one strict UTF-16 scalar from the beginning of a code-unit range.
        /// @param codeUnits Range beginning with the scalar to decode.
        /// @return Decoded, incomplete, or invalid result.
        /// @note A trailing high surrogate is incomplete.
        /// @note Incomplete and invalid results return scalar U+0000 and consume zero code units.
        [[nodiscard]] Types::Utf16::DecodeResult decodeScalar(std::span<const char16_t> codeUnits) noexcept;

        /// @brief Encodes one Unicode scalar as UTF-16.
        /// @param scalar Value to encode.
        /// @return Fixed-size encoded result, or InvalidScalar.
        [[nodiscard]] Types::Utf16::EncodeResult encodeScalar(char32_t scalar) noexcept;

        /// @brief Validates an entire UTF-16 code-unit range.
        /// @param text UTF-16 code units to validate.
        /// @return Validation outcome and complete valid-prefix length.
        /// @note An empty range and embedded U+0000 values are valid.
        [[nodiscard]] Types::Utf16::ValidationResult validate(std::span<const char16_t> text) noexcept;

        /// @brief Measures the UTF-8 storage required for strict UTF-16 input.
        /// @param source UTF-16 source text.
        /// @return Measurement outcome, measured source prefix, and required UTF-8 bytes.
        /// @note On source failure, progress describes only the complete valid prefix before the failing sequence.
        [[nodiscard]] Types::Utf16::ToUtf8MeasureResult measureToUtf8(std::span<const char16_t> source) noexcept;

        /// @brief Converts strict UTF-16 input to caller-provided UTF-8 storage.
        /// @param source UTF-16 source text.
        /// @param destination Destination UTF-8 byte storage.
        /// @return Conversion outcome and completed source/output progress.
        /// @note No partial UTF-8 sequence is written.
        /// @note No terminating U+0000 byte is appended.
        /// @note Destination elements after bytesWritten remain untouched.
        /// @note Completed progress is preserved before source failure or destination exhaustion.
        /// @note Overlapping source and destination memory is rejected before any output is written.
        [[nodiscard]] Types::Utf16::ToUtf8Result convertToUtf8(std::span<const char16_t> source, std::span<char> destination) noexcept;
    } // namespace Utf16

    /// @}
} // namespace GameWIP::Unicode
