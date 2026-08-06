/// @file unicode.h
/// @brief Public API for the Unicode foundation library.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

/// @brief Platform-neutral Unicode encoding, validation, conversion, and text-boundary primitives.
/// @details GameWIP text uses UTF-8 as its canonical public representation. This library provides strict,
/// non-allocating Unicode primitives and a portable UTF-16 bridge without defining recovery, editing,
/// rendering, locale, normalization, or terminal-cell-width policy.
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

    /// @brief Passive Unicode values, outcomes, and results.
    namespace Types
    {
        /// @brief Outcome of decoding one encoded Unicode scalar.
        enum class DecodeOutcome : std::uint8_t
        {
            /// @brief One complete Unicode scalar was decoded.
            Decoded,

            /// @brief The supplied input is a valid prefix but requires additional code units.
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

        /// @brief Result of decoding one UTF-8 scalar.
        struct Utf8DecodeResult
        {
            /// @brief Decoded scalar when outcome is Decoded.
            char32_t scalar = U'\0';

            /// @brief Number of UTF-8 bytes forming scalar when outcome is Decoded.
            std::uint8_t bytesConsumed = 0;

            /// @brief Classification of the decode attempt.
            DecodeOutcome outcome = DecodeOutcome::Incomplete;
        };

        /// @brief Fixed-size UTF-8 encoding of one Unicode scalar.
        struct Utf8EncodeResult
        {
            /// @brief Encoded UTF-8 bytes when outcome is Encoded.
            std::array<char, Utf8::kMaximumScalarBytes> bytes{};

            /// @brief Number of valid elements stored in bytes.
            std::uint8_t byteCount = 0;

            /// @brief Classification of the encode attempt.
            EncodeOutcome outcome = EncodeOutcome::InvalidScalar;
        };

        /// @brief Result of validating an entire UTF-8 range.
        struct Utf8ValidationResult
        {
            /// @brief Number of leading bytes forming complete valid UTF-8 sequences.
            std::size_t validPrefixBytes = 0;

            /// @brief Classification of the complete range.
            ValidationOutcome outcome = ValidationOutcome::Valid;
        };

        /// @brief Result of decoding one UTF-16 scalar.
        struct Utf16DecodeResult
        {
            /// @brief Decoded scalar when outcome is Decoded.
            char32_t scalar = U'\0';

            /// @brief Number of UTF-16 code units forming scalar when outcome is Decoded.
            std::uint8_t codeUnitsConsumed = 0;

            /// @brief Classification of the decode attempt.
            DecodeOutcome outcome = DecodeOutcome::Incomplete;
        };

        /// @brief Fixed-size UTF-16 encoding of one Unicode scalar.
        struct Utf16EncodeResult
        {
            /// @brief Encoded UTF-16 code units when outcome is Encoded.
            std::array<char16_t, Utf16::kMaximumScalarCodeUnits> codeUnits{};

            /// @brief Number of valid elements stored in codeUnits.
            std::uint8_t codeUnitCount = 0;

            /// @brief Classification of the encode attempt.
            EncodeOutcome outcome = EncodeOutcome::InvalidScalar;
        };

        /// @brief Result of validating an entire UTF-16 range.
        struct Utf16ValidationResult
        {
            /// @brief Number of leading code units forming complete valid UTF-16 sequences.
            std::size_t validPrefixCodeUnits = 0;

            /// @brief Classification of the complete range.
            ValidationOutcome outcome = ValidationOutcome::Valid;
        };

        /// @brief Result of measuring a UTF-8 to UTF-16 conversion.
        struct Utf8ToUtf16MeasureResult
        {
            /// @brief Number of source bytes included in the measured valid prefix.
            std::size_t sourceBytesProcessed = 0;

            /// @brief UTF-16 code units required to store the measured valid prefix.
            std::size_t requiredCodeUnits = 0;

            /// @brief Classification of the measurement attempt.
            MeasureOutcome outcome = MeasureOutcome::Measured;
        };

        /// @brief Result of measuring a UTF-16 to UTF-8 conversion.
        struct Utf16ToUtf8MeasureResult
        {
            /// @brief Number of source code units included in the measured valid prefix.
            std::size_t sourceCodeUnitsProcessed = 0;

            /// @brief UTF-8 bytes required to store the measured valid prefix.
            std::size_t requiredBytes = 0;

            /// @brief Classification of the measurement attempt.
            MeasureOutcome outcome = MeasureOutcome::Measured;
        };

        /// @brief Result of converting UTF-8 to UTF-16.
        struct Utf8ToUtf16Result
        {
            /// @brief Number of complete source bytes consumed.
            std::size_t sourceBytesConsumed = 0;

            /// @brief Number of UTF-16 code units written.
            std::size_t codeUnitsWritten = 0;

            /// @brief Classification of the conversion attempt.
            ConversionOutcome outcome = ConversionOutcome::Converted;
        };

        /// @brief Result of converting UTF-16 to UTF-8.
        struct Utf16ToUtf8Result
        {
            /// @brief Number of complete source code units consumed.
            std::size_t sourceCodeUnitsConsumed = 0;

            /// @brief Number of UTF-8 bytes written.
            std::size_t bytesWritten = 0;

            /// @brief Classification of the conversion attempt.
            ConversionOutcome outcome = ConversionOutcome::Converted;
        };

        /// @brief Result of traversing a UTF-8 code-point or grapheme boundary.
        struct Utf8BoundaryResult
        {
            /// @brief Resulting byte offset.
            /// @details On Found, this is the discovered boundary. On AtBeginning or AtEnd, this is the
            /// unchanged endpoint. On failure, this is the original caller-provided offset.
            std::size_t byteOffset = 0;

            /// @brief Classification of the traversal attempt.
            BoundaryOutcome outcome = BoundaryOutcome::InvalidOffset;
        };

        /// @brief Unicode Standard version implemented by the library's generated data.
        struct UnicodeVersion
        {
            /// @brief Major Unicode Standard version.
            std::uint8_t major = 0;

            /// @brief Minor Unicode Standard version.
            std::uint8_t minor = 0;

            /// @brief Patch Unicode Standard version.
            std::uint8_t patch = 0;
        };
    } // namespace Types

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
    [[nodiscard]] Types::UnicodeVersion getStandardVersion() noexcept;

    /// @brief Strict UTF-8 encoding, validation, conversion, and boundary operations.
    namespace Utf8
    {
        /// @brief Decodes one strict UTF-8 sequence from the beginning of a byte range.
        /// @param bytes Range beginning with the sequence to decode.
        /// @return Decoded, incomplete, or invalid result.
        /// @note Incomplete and invalid results return scalar U+0000 and consume zero bytes.
        [[nodiscard]] Types::Utf8DecodeResult decodeScalar(std::string_view bytes) noexcept;

        /// @brief Encodes one Unicode scalar as UTF-8.
        /// @param scalar Value to encode.
        /// @return Fixed-size encoded result, or InvalidScalar.
        [[nodiscard]] Types::Utf8EncodeResult encodeScalar(char32_t scalar) noexcept;

        /// @brief Validates an entire UTF-8 byte range.
        /// @param text UTF-8 bytes to validate.
        /// @return Validation outcome and complete valid-prefix length.
        /// @note An empty range and embedded U+0000 values are valid.
        [[nodiscard]] Types::Utf8ValidationResult validate(std::string_view text) noexcept;

        /// @brief Measures the UTF-16 storage required for strict UTF-8 input.
        /// @param source UTF-8 source text.
        /// @return Measurement outcome, measured source prefix, and required UTF-16 code units.
        [[nodiscard]] Types::Utf8ToUtf16MeasureResult measureToUtf16(std::string_view source) noexcept;

        /// @brief Converts strict UTF-8 input to caller-provided UTF-16 storage.
        /// @param source UTF-8 source text.
        /// @param destination Destination UTF-16 storage.
        /// @return Conversion outcome and completed source/output progress.
        /// @note No partial scalar or surrogate pair is written.
        /// @note No terminating U+0000 code unit is appended.
        /// @note Destination elements after codeUnitsWritten remain untouched.
        [[nodiscard]] Types::Utf8ToUtf16Result convertToUtf16(std::string_view source, std::span<char16_t> destination) noexcept;

        /// @brief Finds the next UTF-8 code-point boundary after a byte offset.
        /// @param text UTF-8 text to traverse.
        /// @param byteOffset Current byte offset.
        /// @return The next boundary or a deterministic endpoint/error result.
        /// @note byteOffset must identify a UTF-8 code-point boundary and may equal text.size().
        [[nodiscard]] Types::Utf8BoundaryResult nextCodePointBoundary(std::string_view text, std::size_t byteOffset) noexcept;

        /// @brief Finds the previous UTF-8 code-point boundary before a byte offset.
        /// @param text UTF-8 text to traverse.
        /// @param byteOffset Current byte offset.
        /// @return The previous boundary or a deterministic endpoint/error result.
        /// @note byteOffset must identify a UTF-8 code-point boundary and may equal text.size().
        [[nodiscard]] Types::Utf8BoundaryResult previousCodePointBoundary(std::string_view text, std::size_t byteOffset) noexcept;

        /// @brief Finds the next extended grapheme-cluster boundary after a byte offset.
        /// @param text UTF-8 text to traverse.
        /// @param byteOffset Current code-point-aligned byte offset.
        /// @return The next grapheme boundary or a deterministic endpoint/error result.
        /// @details When byteOffset lies inside an extended grapheme cluster, the returned boundary is
        /// the end of that containing cluster.
        [[nodiscard]] Types::Utf8BoundaryResult nextGraphemeBoundary(std::string_view text, std::size_t byteOffset) noexcept;

        /// @brief Finds the previous extended grapheme-cluster boundary before a byte offset.
        /// @param text UTF-8 text to traverse.
        /// @param byteOffset Current code-point-aligned byte offset.
        /// @return The previous grapheme boundary or a deterministic endpoint/error result.
        /// @details When byteOffset lies inside an extended grapheme cluster, the returned boundary is
        /// the beginning of that containing cluster.
        [[nodiscard]] Types::Utf8BoundaryResult previousGraphemeBoundary(std::string_view text, std::size_t byteOffset) noexcept;
    } // namespace Utf8

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
        [[nodiscard]] Types::Utf16DecodeResult decodeScalar(std::span<const char16_t> codeUnits) noexcept;

        /// @brief Encodes one Unicode scalar as UTF-16.
        /// @param scalar Value to encode.
        /// @return Fixed-size encoded result, or InvalidScalar.
        [[nodiscard]] Types::Utf16EncodeResult encodeScalar(char32_t scalar) noexcept;

        /// @brief Validates an entire UTF-16 code-unit range.
        /// @param text UTF-16 code units to validate.
        /// @return Validation outcome and complete valid-prefix length.
        /// @note An empty range and embedded U+0000 values are valid.
        [[nodiscard]] Types::Utf16ValidationResult validate(std::span<const char16_t> text) noexcept;

        /// @brief Measures the UTF-8 storage required for strict UTF-16 input.
        /// @param source UTF-16 source text.
        /// @return Measurement outcome, measured source prefix, and required UTF-8 bytes.
        [[nodiscard]] Types::Utf16ToUtf8MeasureResult measureToUtf8(std::span<const char16_t> source) noexcept;

        /// @brief Converts strict UTF-16 input to caller-provided UTF-8 storage.
        /// @param source UTF-16 source text.
        /// @param destination Destination UTF-8 byte storage.
        /// @return Conversion outcome and completed source/output progress.
        /// @note No partial UTF-8 sequence is written.
        /// @note No terminating U+0000 byte is appended.
        /// @note Destination elements after bytesWritten remain untouched.
        [[nodiscard]] Types::Utf16ToUtf8Result convertToUtf8(std::span<const char16_t> source, std::span<char> destination) noexcept;
    } // namespace Utf16
} // namespace GameWIP::Unicode