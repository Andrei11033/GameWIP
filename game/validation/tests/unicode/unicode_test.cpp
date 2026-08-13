/// @file unicode_test.cpp
/// @brief Unicode correctness, exhaustive scalar, generated-data, and grapheme-conformance tests.

#include "validation/tests/unicode/unicode_test.h"

#include "test_support/test_support.h"
#include "unicode/internal/generated/unicode_properties.h"
#include "unicode/internal/type_aliases.h"
#include "unicode/internal/unicode_properties.h"
#include "unicode/unicode.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <initializer_list>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace
{
    namespace Unicode = GameWIP::Unicode;
    namespace TestSupport = GameWIP::TestSupport;

    using BoundaryOutcome = Unicode::Types::BoundaryOutcome;
    using ConversionOutcome = Unicode::Types::ConversionOutcome;
    using DecodeOutcome = Unicode::Types::DecodeOutcome;
    using EncodeOutcome = Unicode::Types::EncodeOutcome;
    using MeasureOutcome = Unicode::Types::MeasureOutcome;
    using ValidationOutcome = Unicode::Types::ValidationOutcome;

    static_assert(Unicode::Utf8::kMaximumScalarBytes == 4);
    static_assert(Unicode::Utf16::kMaximumScalarCodeUnits == 2);
    static_assert(Unicode::isScalarValue(U'\0'));
    static_assert(Unicode::isScalarValue(static_cast<char32_t>(0xD7FF)));
    static_assert(!Unicode::isScalarValue(static_cast<char32_t>(0xD800)));
    static_assert(!Unicode::isScalarValue(static_cast<char32_t>(0xDFFF)));
    static_assert(Unicode::isScalarValue(static_cast<char32_t>(0xE000)));
    static_assert(Unicode::isScalarValue(Unicode::kMaximumScalarValue));
    static_assert(!Unicode::isScalarValue(static_cast<char32_t>(0x110000)));
    static_assert(noexcept(Unicode::getStandardVersion()));
    static_assert(noexcept(Unicode::Utf8::decodeScalar(std::declval<std::string_view>())));
    static_assert(noexcept(Unicode::Utf8::encodeScalar(U'A')));
    static_assert(noexcept(Unicode::Utf8::validate(std::declval<std::string_view>())));
    static_assert(noexcept(Unicode::Utf8::measureToUtf16(std::declval<std::string_view>())));
    static_assert(noexcept(Unicode::Utf8::convertToUtf16(std::declval<std::string_view>(), std::declval<std::span<char16_t>>())));
    static_assert(noexcept(Unicode::Utf8::nextCodePointBoundary(std::declval<std::string_view>(), 0)));
    static_assert(noexcept(Unicode::Utf8::previousCodePointBoundary(std::declval<std::string_view>(), 0)));
    static_assert(noexcept(Unicode::Utf8::nextGraphemeBoundary(std::declval<std::string_view>(), 0)));
    static_assert(noexcept(Unicode::Utf8::previousGraphemeBoundary(std::declval<std::string_view>(), 0)));
    static_assert(noexcept(Unicode::Utf16::decodeScalar(std::declval<std::span<const char16_t>>())));
    static_assert(noexcept(Unicode::Utf16::encodeScalar(U'A')));
    static_assert(noexcept(Unicode::Utf16::validate(std::declval<std::span<const char16_t>>())));
    static_assert(noexcept(Unicode::Utf16::measureToUtf8(std::declval<std::span<const char16_t>>())));
    static_assert(noexcept(Unicode::Utf16::convertToUtf8(std::declval<std::span<const char16_t>>(), std::declval<std::span<char>>())));

    /// @brief Builds a byte string from explicit unsigned octets without depending on char signedness.
    std::string bytes(std::initializer_list<std::uint8_t> values)
    {
        std::string result;
        result.reserve(values.size());
        for (const std::uint8_t value : values)
        {
            result.push_back(static_cast<char>(value));
        }
        return result;
    }

    /// @brief Returns the valid UTF-8 prefix stored in one scalar-encode result.
    std::string_view encodedBytes(const Unicode::Types::Utf8EncodeResult &result) noexcept
    {
        return {result.bytes.data(), result.byteCount};
    }

    /// @brief Returns the valid UTF-16 prefix stored in one scalar-encode result.
    std::span<const char16_t> encodedCodeUnits(const Unicode::Types::Utf16EncodeResult &result) noexcept
    {
        return {result.codeUnits.data(), result.codeUnitCount};
    }

    /// @brief Verifies one UTF-8 decode attempt and all deterministic failure fields.
    void expectUtf8Decode(
        TestSupport::Context &context,
        std::string_view name,
        std::string_view input,
        DecodeOutcome expectedOutcome,
        char32_t expectedScalar = U'\0',
        std::uint8_t expectedBytesConsumed = 0)
    {
        const Unicode::Types::Utf8DecodeResult result = Unicode::Utf8::decodeScalar(input);
        static_cast<void>(context.expectEq(std::format("{} outcome", name), expectedOutcome, result.outcome));
        static_cast<void>(context.expectEq(std::format("{} scalar", name), expectedScalar, result.scalar));
        static_cast<void>(context.expectEq(std::format("{} consumed", name), expectedBytesConsumed, result.bytesConsumed));
    }

    /// @brief Verifies one UTF-16 decode attempt and all deterministic failure fields.
    void expectUtf16Decode(
        TestSupport::Context &context,
        std::string_view name,
        std::span<const char16_t> input,
        DecodeOutcome expectedOutcome,
        char32_t expectedScalar = U'\0',
        std::uint8_t expectedCodeUnitsConsumed = 0)
    {
        const Unicode::Types::Utf16DecodeResult result = Unicode::Utf16::decodeScalar(input);
        static_cast<void>(context.expectEq(std::format("{} outcome", name), expectedOutcome, result.outcome));
        static_cast<void>(context.expectEq(std::format("{} scalar", name), expectedScalar, result.scalar));
        static_cast<void>(context.expectEq(std::format("{} consumed", name), expectedCodeUnitsConsumed, result.codeUnitsConsumed));
    }

    /// @brief Verifies standard-version reporting and scalar/surrogate classification boundaries.
    void testVersionAndScalarPredicates(TestSupport::Context &context)
    {
        const Unicode::Types::UnicodeVersion version = Unicode::getStandardVersion();
        static_cast<void>(context.expectEq("Unicode major version", std::uint8_t{17}, version.major));
        static_cast<void>(context.expectEq("Unicode minor version", std::uint8_t{0}, version.minor));
        static_cast<void>(context.expectEq("Unicode patch version", std::uint8_t{0}, version.patch));

        static_cast<void>(context.expectTrue("U+0000 is a scalar", Unicode::isScalarValue(U'\0')));
        static_cast<void>(context.expectTrue("U+D7FF is a scalar", Unicode::isScalarValue(static_cast<char32_t>(0xD7FF))));
        static_cast<void>(context.expectFalse("U+D800 is not a scalar", Unicode::isScalarValue(static_cast<char32_t>(0xD800))));
        static_cast<void>(context.expectFalse("U+DFFF is not a scalar", Unicode::isScalarValue(static_cast<char32_t>(0xDFFF))));
        static_cast<void>(context.expectTrue("U+E000 is a scalar", Unicode::isScalarValue(static_cast<char32_t>(0xE000))));
        static_cast<void>(context.expectTrue("U+10FFFF is a scalar", Unicode::isScalarValue(static_cast<char32_t>(0x10FFFF))));
        static_cast<void>(context.expectFalse("U+110000 is not a scalar", Unicode::isScalarValue(static_cast<char32_t>(0x110000))));

        static_cast<void>(context.expectFalse("D7FF is not a high surrogate", Unicode::Utf16::isHighSurrogate(static_cast<char16_t>(0xD7FF))));
        static_cast<void>(context.expectTrue("D800 is a high surrogate", Unicode::Utf16::isHighSurrogate(static_cast<char16_t>(0xD800))));
        static_cast<void>(context.expectTrue("DBFF is a high surrogate", Unicode::Utf16::isHighSurrogate(static_cast<char16_t>(0xDBFF))));
        static_cast<void>(context.expectFalse("DC00 is not a high surrogate", Unicode::Utf16::isHighSurrogate(static_cast<char16_t>(0xDC00))));
        static_cast<void>(context.expectTrue("DC00 is a low surrogate", Unicode::Utf16::isLowSurrogate(static_cast<char16_t>(0xDC00))));
        static_cast<void>(context.expectTrue("DFFF is a low surrogate", Unicode::Utf16::isLowSurrogate(static_cast<char16_t>(0xDFFF))));
        static_cast<void>(context.expectFalse("E000 is not a low surrogate", Unicode::Utf16::isLowSurrogate(static_cast<char16_t>(0xE000))));
        static_cast<void>(context.expectTrue("D800 is a surrogate", Unicode::Utf16::isSurrogate(static_cast<char16_t>(0xD800))));
        static_cast<void>(context.expectTrue("DFFF is a surrogate", Unicode::Utf16::isSurrogate(static_cast<char16_t>(0xDFFF))));
        static_cast<void>(context.expectFalse("E000 is not a surrogate", Unicode::Utf16::isSurrogate(static_cast<char16_t>(0xE000))));
    }

    /// @brief Verifies strict UTF-8 decode, encode, and whole-range validation edge cases.
    void testUtf8CodecAndValidation(TestSupport::Context &context)
    {
        expectUtf8Decode(context, "empty UTF-8", {}, DecodeOutcome::Incomplete);
        expectUtf8Decode(context, "U+0000 UTF-8", bytes({0x00}), DecodeOutcome::Decoded, U'\0', 1);
        expectUtf8Decode(context, "ASCII UTF-8", bytes({0x41}), DecodeOutcome::Decoded, U'A', 1);
        expectUtf8Decode(context, "U+007F UTF-8", bytes({0x7F}), DecodeOutcome::Decoded, static_cast<char32_t>(0x7F), 1);
        expectUtf8Decode(context, "U+0080 UTF-8", bytes({0xC2, 0x80}), DecodeOutcome::Decoded, static_cast<char32_t>(0x80), 2);
        expectUtf8Decode(context, "U+07FF UTF-8", bytes({0xDF, 0xBF}), DecodeOutcome::Decoded, static_cast<char32_t>(0x7FF), 2);
        expectUtf8Decode(context, "U+0800 UTF-8", bytes({0xE0, 0xA0, 0x80}), DecodeOutcome::Decoded, static_cast<char32_t>(0x800), 3);
        expectUtf8Decode(context, "U+D7FF UTF-8", bytes({0xED, 0x9F, 0xBF}), DecodeOutcome::Decoded, static_cast<char32_t>(0xD7FF), 3);
        expectUtf8Decode(context, "U+E000 UTF-8", bytes({0xEE, 0x80, 0x80}), DecodeOutcome::Decoded, static_cast<char32_t>(0xE000), 3);
        expectUtf8Decode(context, "U+FFFF UTF-8", bytes({0xEF, 0xBF, 0xBF}), DecodeOutcome::Decoded, static_cast<char32_t>(0xFFFF), 3);
        expectUtf8Decode(context, "U+10000 UTF-8", bytes({0xF0, 0x90, 0x80, 0x80}), DecodeOutcome::Decoded, static_cast<char32_t>(0x10000), 4);
        expectUtf8Decode(context, "U+10FFFF UTF-8", bytes({0xF4, 0x8F, 0xBF, 0xBF}), DecodeOutcome::Decoded, static_cast<char32_t>(0x10FFFF), 4);

        for (const std::string &input : {
                 bytes({0xC2}),
                 bytes({0xE0}),
                 bytes({0xE0, 0xA0}),
                 bytes({0xED, 0x9F}),
                 bytes({0xF0}),
                 bytes({0xF0, 0x90}),
                 bytes({0xF0, 0x90, 0x80}),
                 bytes({0xF4, 0x8F, 0xBF}),
             })
        {
            expectUtf8Decode(context, "valid incomplete UTF-8 prefix", input, DecodeOutcome::Incomplete);
        }

        for (const std::string &input : {
                 bytes({0x80}),
                 bytes({0xBF}),
                 bytes({0xC0}),
                 bytes({0xC1}),
                 bytes({0xF5}),
                 bytes({0xFF}),
                 bytes({0xC2, 0x20}),
                 bytes({0xE2, 0x28, 0xA1}),
                 bytes({0xF0, 0x9F, 0x28, 0x80}),
                 bytes({0xC0, 0x80}),
                 bytes({0xE0, 0x80, 0x80}),
                 bytes({0xED, 0xA0, 0x80}),
                 bytes({0xF0, 0x80, 0x80, 0x80}),
                 bytes({0xF4, 0x90, 0x80, 0x80}),
             })
        {
            expectUtf8Decode(context, "strict-invalid UTF-8", input, DecodeOutcome::InvalidEncoding);
        }

        expectUtf8Decode(context, "decoder stops after first scalar", bytes({0x41, 0xFF}), DecodeOutcome::Decoded, U'A', 1);

        struct EncodeCase
        {
            char32_t scalar;
            std::string expected;
        };
        const std::array encodeCases{
            EncodeCase{U'\0', bytes({0x00})},
            EncodeCase{static_cast<char32_t>(0x7F), bytes({0x7F})},
            EncodeCase{static_cast<char32_t>(0x80), bytes({0xC2, 0x80})},
            EncodeCase{static_cast<char32_t>(0x7FF), bytes({0xDF, 0xBF})},
            EncodeCase{static_cast<char32_t>(0x800), bytes({0xE0, 0xA0, 0x80})},
            EncodeCase{static_cast<char32_t>(0xFFFF), bytes({0xEF, 0xBF, 0xBF})},
            EncodeCase{static_cast<char32_t>(0x10000), bytes({0xF0, 0x90, 0x80, 0x80})},
            EncodeCase{static_cast<char32_t>(0x10FFFF), bytes({0xF4, 0x8F, 0xBF, 0xBF})},
        };
        for (const EncodeCase &testCase : encodeCases)
        {
            const Unicode::Types::Utf8EncodeResult encoded = Unicode::Utf8::encodeScalar(testCase.scalar);
            static_cast<void>(context.expectEq("UTF-8 encode succeeds", EncodeOutcome::Encoded, encoded.outcome));
            static_cast<void>(context.expectEq("UTF-8 encode byte count", testCase.expected.size(), static_cast<std::size_t>(encoded.byteCount)));
            static_cast<void>(context.expectEq("UTF-8 encode bytes", std::string_view(testCase.expected), encodedBytes(encoded)));
        }
        for (const char32_t invalidScalar : {
                 static_cast<char32_t>(0xD800),
                 static_cast<char32_t>(0xDFFF),
                 static_cast<char32_t>(0x110000),
             })
        {
            const Unicode::Types::Utf8EncodeResult encoded = Unicode::Utf8::encodeScalar(invalidScalar);
            static_cast<void>(context.expectEq("invalid scalar UTF-8 outcome", EncodeOutcome::InvalidScalar, encoded.outcome));
            static_cast<void>(context.expectEq("invalid scalar UTF-8 byte count", std::uint8_t{0}, encoded.byteCount));
            static_cast<void>(context.expectTrue(
                "invalid scalar UTF-8 storage remains zero",
                std::ranges::all_of(
                    encoded.bytes,
                    [](char value)
                    {
                        return value == '\0';
                    })));
        }

        const std::string validText = bytes({0x00, 0x41, 0xC2, 0x80, 0xE2, 0x82, 0xAC, 0xF0, 0x9F, 0x98, 0x80});
        const Unicode::Types::Utf8ValidationResult valid = Unicode::Utf8::validate(validText);
        static_cast<void>(context.expectEq("valid UTF-8 range outcome", ValidationOutcome::Valid, valid.outcome));
        static_cast<void>(context.expectEq("valid UTF-8 range prefix", validText.size(), valid.validPrefixBytes));

        const Unicode::Types::Utf8ValidationResult empty = Unicode::Utf8::validate({});
        static_cast<void>(context.expectEq("empty UTF-8 range outcome", ValidationOutcome::Valid, empty.outcome));
        static_cast<void>(context.expectEq("empty UTF-8 range prefix", std::size_t{0}, empty.validPrefixBytes));

        const std::string incompleteText = std::string("A") + bytes({0xE2, 0x82});
        const Unicode::Types::Utf8ValidationResult incomplete = Unicode::Utf8::validate(incompleteText);
        static_cast<void>(context.expectEq("incomplete UTF-8 validation outcome", ValidationOutcome::Incomplete, incomplete.outcome));
        static_cast<void>(context.expectEq("incomplete UTF-8 validation prefix", std::size_t{1}, incomplete.validPrefixBytes));

        const std::string invalidText = std::string("A") + bytes({0xE2, 0x28, 0xA1});
        const Unicode::Types::Utf8ValidationResult invalid = Unicode::Utf8::validate(invalidText);
        static_cast<void>(context.expectEq("invalid UTF-8 validation outcome", ValidationOutcome::InvalidEncoding, invalid.outcome));
        static_cast<void>(context.expectEq("invalid UTF-8 validation prefix", std::size_t{1}, invalid.validPrefixBytes));
    }

    /// @brief Verifies strict UTF-16 decode, encode, and whole-range validation edge cases.
    void testUtf16CodecAndValidation(TestSupport::Context &context)
    {
        expectUtf16Decode(context, "empty UTF-16", {}, DecodeOutcome::Incomplete);

        const std::array<char16_t, 1> nul{u'\0'};
        expectUtf16Decode(context, "U+0000 UTF-16", nul, DecodeOutcome::Decoded, U'\0', 1);

        const std::array<char16_t, 1> bmp{static_cast<char16_t>(0xD7FF)};
        expectUtf16Decode(context, "BMP UTF-16", bmp, DecodeOutcome::Decoded, static_cast<char32_t>(0xD7FF), 1);

        const std::array<char16_t, 2> supplementary{static_cast<char16_t>(0xD800), static_cast<char16_t>(0xDC00)};
        expectUtf16Decode(context, "U+10000 UTF-16", supplementary, DecodeOutcome::Decoded, static_cast<char32_t>(0x10000), 2);

        const std::array<char16_t, 2> maximum{static_cast<char16_t>(0xDBFF), static_cast<char16_t>(0xDFFF)};
        expectUtf16Decode(context, "U+10FFFF UTF-16", maximum, DecodeOutcome::Decoded, static_cast<char32_t>(0x10FFFF), 2);

        const std::array<char16_t, 1> danglingHigh{static_cast<char16_t>(0xD800)};
        expectUtf16Decode(context, "dangling high surrogate", danglingHigh, DecodeOutcome::Incomplete);

        const std::array<char16_t, 1> isolatedLow{static_cast<char16_t>(0xDC00)};
        expectUtf16Decode(context, "isolated low surrogate", isolatedLow, DecodeOutcome::InvalidEncoding);

        const std::array<char16_t, 2> highThenBmp{static_cast<char16_t>(0xD800), u'A'};
        expectUtf16Decode(context, "high surrogate followed by BMP", highThenBmp, DecodeOutcome::InvalidEncoding);

        struct EncodeCase
        {
            char32_t scalar;
            std::array<char16_t, 2> expected;
            std::uint8_t count;
        };
        const std::array encodeCases{
            EncodeCase{U'\0', {u'\0', u'\0'}, 1},
            EncodeCase{static_cast<char32_t>(0xD7FF), {static_cast<char16_t>(0xD7FF), u'\0'}, 1},
            EncodeCase{static_cast<char32_t>(0xE000), {static_cast<char16_t>(0xE000), u'\0'}, 1},
            EncodeCase{static_cast<char32_t>(0xFFFF), {static_cast<char16_t>(0xFFFF), u'\0'}, 1},
            EncodeCase{static_cast<char32_t>(0x10000), {static_cast<char16_t>(0xD800), static_cast<char16_t>(0xDC00)}, 2},
            EncodeCase{static_cast<char32_t>(0x10FFFF), {static_cast<char16_t>(0xDBFF), static_cast<char16_t>(0xDFFF)}, 2},
        };
        for (const EncodeCase &testCase : encodeCases)
        {
            const Unicode::Types::Utf16EncodeResult encoded = Unicode::Utf16::encodeScalar(testCase.scalar);
            static_cast<void>(context.expectEq("UTF-16 encode succeeds", EncodeOutcome::Encoded, encoded.outcome));
            static_cast<void>(context.expectEq("UTF-16 encode code-unit count", testCase.count, encoded.codeUnitCount));
            static_cast<void>(context.expectTrue(
                "UTF-16 encode code units",
                std::equal(testCase.expected.begin(), testCase.expected.begin() + testCase.count, encoded.codeUnits.begin())));
        }
        for (const char32_t invalidScalar : {
                 static_cast<char32_t>(0xD800),
                 static_cast<char32_t>(0xDFFF),
                 static_cast<char32_t>(0x110000),
             })
        {
            const Unicode::Types::Utf16EncodeResult encoded = Unicode::Utf16::encodeScalar(invalidScalar);
            static_cast<void>(context.expectEq("invalid scalar UTF-16 outcome", EncodeOutcome::InvalidScalar, encoded.outcome));
            static_cast<void>(context.expectEq("invalid scalar UTF-16 code-unit count", std::uint8_t{0}, encoded.codeUnitCount));
            static_cast<void>(context.expectTrue(
                "invalid scalar UTF-16 storage remains zero",
                std::ranges::all_of(
                    encoded.codeUnits,
                    [](char16_t value)
                    {
                        return value == u'\0';
                    })));
        }

        const std::array<char16_t, 5> validText{
            u'\0',
            u'A',
            static_cast<char16_t>(0x20AC),
            static_cast<char16_t>(0xD83D),
            static_cast<char16_t>(0xDE00),
        };
        const Unicode::Types::Utf16ValidationResult valid = Unicode::Utf16::validate(validText);
        static_cast<void>(context.expectEq("valid UTF-16 range outcome", ValidationOutcome::Valid, valid.outcome));
        static_cast<void>(context.expectEq("valid UTF-16 range prefix", validText.size(), valid.validPrefixCodeUnits));

        const Unicode::Types::Utf16ValidationResult empty = Unicode::Utf16::validate({});
        static_cast<void>(context.expectEq("empty UTF-16 range outcome", ValidationOutcome::Valid, empty.outcome));
        static_cast<void>(context.expectEq("empty UTF-16 range prefix", std::size_t{0}, empty.validPrefixCodeUnits));

        const std::array<char16_t, 2> incompleteText{u'A', static_cast<char16_t>(0xD800)};
        const Unicode::Types::Utf16ValidationResult incomplete = Unicode::Utf16::validate(incompleteText);
        static_cast<void>(context.expectEq("incomplete UTF-16 validation outcome", ValidationOutcome::Incomplete, incomplete.outcome));
        static_cast<void>(context.expectEq("incomplete UTF-16 validation prefix", std::size_t{1}, incomplete.validPrefixCodeUnits));

        const std::array<char16_t, 3> invalidText{u'A', static_cast<char16_t>(0xD800), u'B'};
        const Unicode::Types::Utf16ValidationResult invalid = Unicode::Utf16::validate(invalidText);
        static_cast<void>(context.expectEq("invalid UTF-16 validation outcome", ValidationOutcome::InvalidEncoding, invalid.outcome));
        static_cast<void>(context.expectEq("invalid UTF-16 validation prefix", std::size_t{1}, invalid.validPrefixCodeUnits));
    }

    /// @brief Verifies conversion measurement, exact/partial writes, sentinels, malformed input, and overlap rejection.
    void testConversions(TestSupport::Context &context)
    {
        const std::string utf8 = bytes({0x41, 0xC2, 0xA2, 0xE2, 0x82, 0xAC, 0xF0, 0x9F, 0x98, 0x80});
        const std::array<char16_t, 5> utf16{
            u'A',
            static_cast<char16_t>(0x00A2),
            static_cast<char16_t>(0x20AC),
            static_cast<char16_t>(0xD83D),
            static_cast<char16_t>(0xDE00),
        };

        const Unicode::Types::Utf8ToUtf16MeasureResult toUtf16Measure = Unicode::Utf8::measureToUtf16(utf8);
        static_cast<void>(context.expectEq("UTF-8 to UTF-16 measurement outcome", MeasureOutcome::Measured, toUtf16Measure.outcome));
        static_cast<void>(context.expectEq("UTF-8 to UTF-16 measurement source", utf8.size(), toUtf16Measure.sourceBytesProcessed));
        static_cast<void>(context.expectEq("UTF-8 to UTF-16 measurement destination", utf16.size(), toUtf16Measure.requiredCodeUnits));

        const Unicode::Types::Utf16ToUtf8MeasureResult toUtf8Measure = Unicode::Utf16::measureToUtf8(utf16);
        static_cast<void>(context.expectEq("UTF-16 to UTF-8 measurement outcome", MeasureOutcome::Measured, toUtf8Measure.outcome));
        static_cast<void>(context.expectEq("UTF-16 to UTF-8 measurement source", utf16.size(), toUtf8Measure.sourceCodeUnitsProcessed));
        static_cast<void>(context.expectEq("UTF-16 to UTF-8 measurement destination", utf8.size(), toUtf8Measure.requiredBytes));

        const Unicode::Types::Utf8ToUtf16MeasureResult emptyToUtf16Measure = Unicode::Utf8::measureToUtf16({});
        const Unicode::Types::Utf16ToUtf8MeasureResult emptyToUtf8Measure = Unicode::Utf16::measureToUtf8({});
        static_cast<void>(context.expectEq("empty UTF-8 measurement outcome", MeasureOutcome::Measured, emptyToUtf16Measure.outcome));
        static_cast<void>(context.expectEq("empty UTF-8 measurement size", std::size_t{0}, emptyToUtf16Measure.requiredCodeUnits));
        static_cast<void>(context.expectEq("empty UTF-16 measurement outcome", MeasureOutcome::Measured, emptyToUtf8Measure.outcome));
        static_cast<void>(context.expectEq("empty UTF-16 measurement size", std::size_t{0}, emptyToUtf8Measure.requiredBytes));

        const std::string incompleteUtf8 = std::string("A") + bytes({0xE2, 0x82});
        const Unicode::Types::Utf8ToUtf16MeasureResult incompleteUtf8Measure = Unicode::Utf8::measureToUtf16(incompleteUtf8);
        static_cast<void>(context.expectEq("incomplete UTF-8 measurement outcome", MeasureOutcome::Incomplete, incompleteUtf8Measure.outcome));
        static_cast<void>(
            context.expectEq("incomplete UTF-8 measurement source progress", std::size_t{1}, incompleteUtf8Measure.sourceBytesProcessed));
        static_cast<void>(
            context.expectEq("incomplete UTF-8 measurement destination progress", std::size_t{1}, incompleteUtf8Measure.requiredCodeUnits));

        const std::string invalidUtf8 = std::string("A") + bytes({0xE2, 0x28});
        const Unicode::Types::Utf8ToUtf16MeasureResult invalidUtf8Measure = Unicode::Utf8::measureToUtf16(invalidUtf8);
        static_cast<void>(context.expectEq("invalid UTF-8 measurement outcome", MeasureOutcome::InvalidEncoding, invalidUtf8Measure.outcome));
        static_cast<void>(context.expectEq("invalid UTF-8 measurement source progress", std::size_t{1}, invalidUtf8Measure.sourceBytesProcessed));
        static_cast<void>(context.expectEq("invalid UTF-8 measurement destination progress", std::size_t{1}, invalidUtf8Measure.requiredCodeUnits));

        const std::array<char16_t, 2> incompleteUtf16{u'A', static_cast<char16_t>(0xD800)};
        const Unicode::Types::Utf16ToUtf8MeasureResult incompleteUtf16Measure = Unicode::Utf16::measureToUtf8(incompleteUtf16);
        static_cast<void>(context.expectEq("incomplete UTF-16 measurement outcome", MeasureOutcome::Incomplete, incompleteUtf16Measure.outcome));
        static_cast<void>(
            context.expectEq("incomplete UTF-16 measurement source progress", std::size_t{1}, incompleteUtf16Measure.sourceCodeUnitsProcessed));
        static_cast<void>(
            context.expectEq("incomplete UTF-16 measurement destination progress", std::size_t{1}, incompleteUtf16Measure.requiredBytes));

        const std::array<char16_t, 3> invalidUtf16{u'A', static_cast<char16_t>(0xD800), u'B'};
        const Unicode::Types::Utf16ToUtf8MeasureResult invalidUtf16Measure = Unicode::Utf16::measureToUtf8(invalidUtf16);
        static_cast<void>(context.expectEq("invalid UTF-16 measurement outcome", MeasureOutcome::InvalidEncoding, invalidUtf16Measure.outcome));
        static_cast<void>(
            context.expectEq("invalid UTF-16 measurement source progress", std::size_t{1}, invalidUtf16Measure.sourceCodeUnitsProcessed));
        static_cast<void>(context.expectEq("invalid UTF-16 measurement destination progress", std::size_t{1}, invalidUtf16Measure.requiredBytes));

        std::array<char16_t, 5> exactUtf16Destination{};
        const Unicode::Types::Utf8ToUtf16Result exactToUtf16 = Unicode::Utf8::convertToUtf16(utf8, exactUtf16Destination);
        static_cast<void>(context.expectEq("exact UTF-8 to UTF-16 conversion outcome", ConversionOutcome::Converted, exactToUtf16.outcome));
        static_cast<void>(context.expectTrue("exact UTF-8 to UTF-16 payload", std::equal(utf16.begin(), utf16.end(), exactUtf16Destination.begin())));

        std::array<char, 10> exactUtf8Destination{};
        const Unicode::Types::Utf16ToUtf8Result exactToUtf8 = Unicode::Utf16::convertToUtf8(utf16, exactUtf8Destination);
        static_cast<void>(context.expectEq("exact UTF-16 to UTF-8 conversion outcome", ConversionOutcome::Converted, exactToUtf8.outcome));
        static_cast<void>(context.expectEq(
            "exact UTF-16 to UTF-8 payload",
            std::string_view(utf8),
            std::string_view(exactUtf8Destination.data(), exactUtf8Destination.size())));

        std::array<char16_t, 7> utf16Destination;
        utf16Destination.fill(static_cast<char16_t>(0xCAFE));
        const Unicode::Types::Utf8ToUtf16Result toUtf16 = Unicode::Utf8::convertToUtf16(utf8, utf16Destination);
        static_cast<void>(context.expectEq("UTF-8 to UTF-16 conversion outcome", ConversionOutcome::Converted, toUtf16.outcome));
        static_cast<void>(context.expectEq("UTF-8 to UTF-16 source progress", utf8.size(), toUtf16.sourceBytesConsumed));
        static_cast<void>(context.expectEq("UTF-8 to UTF-16 destination progress", utf16.size(), toUtf16.codeUnitsWritten));
        static_cast<void>(context.expectTrue("UTF-8 to UTF-16 exact payload", std::equal(utf16.begin(), utf16.end(), utf16Destination.begin())));
        static_cast<void>(context.expectEq("UTF-8 to UTF-16 leaves first tail sentinel", static_cast<char16_t>(0xCAFE), utf16Destination[5]));
        static_cast<void>(context.expectEq("UTF-8 to UTF-16 leaves second tail sentinel", static_cast<char16_t>(0xCAFE), utf16Destination[6]));

        std::array<char, 12> utf8Destination;
        utf8Destination.fill(static_cast<char>(0x5A));
        const Unicode::Types::Utf16ToUtf8Result toUtf8 = Unicode::Utf16::convertToUtf8(utf16, utf8Destination);
        static_cast<void>(context.expectEq("UTF-16 to UTF-8 conversion outcome", ConversionOutcome::Converted, toUtf8.outcome));
        static_cast<void>(context.expectEq("UTF-16 to UTF-8 source progress", utf16.size(), toUtf8.sourceCodeUnitsConsumed));
        static_cast<void>(context.expectEq("UTF-16 to UTF-8 destination progress", utf8.size(), toUtf8.bytesWritten));
        static_cast<void>(
            context.expectEq("UTF-16 to UTF-8 exact payload", std::string_view(utf8), std::string_view(utf8Destination.data(), utf8.size())));
        static_cast<void>(context.expectEq("UTF-16 to UTF-8 leaves first tail sentinel", static_cast<char>(0x5A), utf8Destination[10]));
        static_cast<void>(context.expectEq("UTF-16 to UTF-8 leaves second tail sentinel", static_cast<char>(0x5A), utf8Destination[11]));

        const std::string scalarBoundaryUtf8 = std::string("A") + bytes({0xF0, 0x9F, 0x98, 0x80}) + "B";
        std::array<char16_t, 4> smallUtf16;
        smallUtf16.fill(static_cast<char16_t>(0xBEEF));
        const Unicode::Types::Utf8ToUtf16Result smallToUtf16 =
            Unicode::Utf8::convertToUtf16(scalarBoundaryUtf8, std::span<char16_t>(smallUtf16).first(2));
        static_cast<void>(
            context.expectEq("UTF-8 conversion stops before partial surrogate pair", ConversionOutcome::DestinationTooSmall, smallToUtf16.outcome));
        static_cast<void>(context.expectEq("UTF-8 conversion partial source progress", std::size_t{1}, smallToUtf16.sourceBytesConsumed));
        static_cast<void>(context.expectEq("UTF-8 conversion partial destination progress", std::size_t{1}, smallToUtf16.codeUnitsWritten));
        static_cast<void>(context.expectEq("UTF-8 conversion preserves unwritten scalar slot", static_cast<char16_t>(0xBEEF), smallUtf16[1]));
        static_cast<void>(context.expectEq("UTF-8 conversion preserves destination tail", static_cast<char16_t>(0xBEEF), smallUtf16[2]));

        const std::array<char16_t, 4> scalarBoundaryUtf16{u'A', static_cast<char16_t>(0xD83D), static_cast<char16_t>(0xDE00), u'B'};
        std::array<char, 8> smallUtf8;
        smallUtf8.fill(static_cast<char>(0x66));
        const Unicode::Types::Utf16ToUtf8Result smallToUtf8 = Unicode::Utf16::convertToUtf8(scalarBoundaryUtf16, std::span<char>(smallUtf8).first(4));
        static_cast<void>(
            context.expectEq("UTF-16 conversion stops before partial UTF-8 scalar", ConversionOutcome::DestinationTooSmall, smallToUtf8.outcome));
        static_cast<void>(context.expectEq("UTF-16 conversion partial source progress", std::size_t{1}, smallToUtf8.sourceCodeUnitsConsumed));
        static_cast<void>(context.expectEq("UTF-16 conversion partial destination progress", std::size_t{1}, smallToUtf8.bytesWritten));
        static_cast<void>(context.expectEq("UTF-16 conversion preserves first unwritten byte", static_cast<char>(0x66), smallUtf8[1]));
        static_cast<void>(context.expectEq("UTF-16 conversion preserves destination tail", static_cast<char>(0x66), smallUtf8[4]));

        std::array<char16_t, 4> incompleteUtf16Destination;
        incompleteUtf16Destination.fill(static_cast<char16_t>(0x3333));
        const Unicode::Types::Utf8ToUtf16Result incompleteUtf8Conversion = Unicode::Utf8::convertToUtf16(incompleteUtf8, incompleteUtf16Destination);
        static_cast<void>(context.expectEq("incomplete UTF-8 conversion outcome", ConversionOutcome::Incomplete, incompleteUtf8Conversion.outcome));
        static_cast<void>(
            context.expectEq("incomplete UTF-8 conversion source progress", std::size_t{1}, incompleteUtf8Conversion.sourceBytesConsumed));
        static_cast<void>(
            context.expectEq("incomplete UTF-8 conversion destination progress", std::size_t{1}, incompleteUtf8Conversion.codeUnitsWritten));
        static_cast<void>(context.expectEq(
            "incomplete UTF-8 conversion preserves destination after prefix",
            static_cast<char16_t>(0x3333),
            incompleteUtf16Destination[1]));

        std::array<char, 8> incompleteUtf8Destination;
        incompleteUtf8Destination.fill(static_cast<char>(0x33));
        const Unicode::Types::Utf16ToUtf8Result incompleteUtf16Conversion = Unicode::Utf16::convertToUtf8(incompleteUtf16, incompleteUtf8Destination);
        static_cast<void>(context.expectEq("incomplete UTF-16 conversion outcome", ConversionOutcome::Incomplete, incompleteUtf16Conversion.outcome));
        static_cast<void>(
            context.expectEq("incomplete UTF-16 conversion source progress", std::size_t{1}, incompleteUtf16Conversion.sourceCodeUnitsConsumed));
        static_cast<void>(
            context.expectEq("incomplete UTF-16 conversion destination progress", std::size_t{1}, incompleteUtf16Conversion.bytesWritten));
        static_cast<void>(context.expectEq(
            "incomplete UTF-16 conversion preserves destination after prefix",
            static_cast<char>(0x33),
            incompleteUtf8Destination[1]));

        std::array<char16_t, 4> malformedUtf16Destination;
        malformedUtf16Destination.fill(static_cast<char16_t>(0x4444));
        const Unicode::Types::Utf8ToUtf16Result invalidUtf8Conversion = Unicode::Utf8::convertToUtf16(invalidUtf8, malformedUtf16Destination);
        static_cast<void>(context.expectEq("invalid UTF-8 conversion outcome", ConversionOutcome::InvalidEncoding, invalidUtf8Conversion.outcome));
        static_cast<void>(context.expectEq("invalid UTF-8 conversion source progress", std::size_t{1}, invalidUtf8Conversion.sourceBytesConsumed));
        static_cast<void>(context.expectEq("invalid UTF-8 conversion destination progress", std::size_t{1}, invalidUtf8Conversion.codeUnitsWritten));
        static_cast<void>(context.expectEq(
            "invalid UTF-8 conversion preserves destination after prefix",
            static_cast<char16_t>(0x4444),
            malformedUtf16Destination[1]));

        std::array<char, 8> malformedUtf8Destination;
        malformedUtf8Destination.fill(static_cast<char>(0x44));
        const Unicode::Types::Utf16ToUtf8Result invalidUtf16Conversion = Unicode::Utf16::convertToUtf8(invalidUtf16, malformedUtf8Destination);
        static_cast<void>(context.expectEq("invalid UTF-16 conversion outcome", ConversionOutcome::InvalidEncoding, invalidUtf16Conversion.outcome));
        static_cast<void>(
            context.expectEq("invalid UTF-16 conversion source progress", std::size_t{1}, invalidUtf16Conversion.sourceCodeUnitsConsumed));
        static_cast<void>(context.expectEq("invalid UTF-16 conversion destination progress", std::size_t{1}, invalidUtf16Conversion.bytesWritten));
        static_cast<void>(
            context.expectEq("invalid UTF-16 conversion preserves destination after prefix", static_cast<char>(0x44), malformedUtf8Destination[1]));

        std::array<char16_t, 1> emptyUtf16Destination{static_cast<char16_t>(0x7777)};
        const Unicode::Types::Utf8ToUtf16Result emptyToUtf16 = Unicode::Utf8::convertToUtf16({}, emptyUtf16Destination);
        static_cast<void>(context.expectEq("empty UTF-8 conversion outcome", ConversionOutcome::Converted, emptyToUtf16.outcome));
        static_cast<void>(context.expectEq("empty UTF-8 conversion writes nothing", std::size_t{0}, emptyToUtf16.codeUnitsWritten));
        static_cast<void>(context.expectEq("empty UTF-8 conversion preserves destination", static_cast<char16_t>(0x7777), emptyUtf16Destination[0]));

        std::array<char, 1> emptyUtf8Destination{static_cast<char>(0x77)};
        const Unicode::Types::Utf16ToUtf8Result emptyToUtf8 = Unicode::Utf16::convertToUtf8({}, emptyUtf8Destination);
        static_cast<void>(context.expectEq("empty UTF-16 conversion outcome", ConversionOutcome::Converted, emptyToUtf8.outcome));
        static_cast<void>(context.expectEq("empty UTF-16 conversion writes nothing", std::size_t{0}, emptyToUtf8.bytesWritten));
        static_cast<void>(context.expectEq("empty UTF-16 conversion preserves destination", static_cast<char>(0x77), emptyUtf8Destination[0]));

        std::array<char16_t, 4> overlapStorage{
            static_cast<char16_t>(0x4141),
            static_cast<char16_t>(0x4242),
            static_cast<char16_t>(0x4343),
            static_cast<char16_t>(0x4444),
        };
        const std::string_view overlappingUtf8(reinterpret_cast<const char *>(overlapStorage.data()), 4);
        const Unicode::Types::Utf8ToUtf16Result overlapToUtf16 = Unicode::Utf8::convertToUtf16(overlappingUtf8, overlapStorage);
        static_cast<void>(context.expectEq("UTF-8 to UTF-16 overlap outcome", ConversionOutcome::OverlappingRanges, overlapToUtf16.outcome));
        static_cast<void>(context.expectEq("UTF-8 to UTF-16 overlap consumes nothing", std::size_t{0}, overlapToUtf16.sourceBytesConsumed));
        static_cast<void>(context.expectEq("UTF-8 to UTF-16 overlap writes nothing", std::size_t{0}, overlapToUtf16.codeUnitsWritten));

        std::array<char16_t, 4> reverseOverlapStorage{u'A', u'B', u'C', u'D'};
        std::span<char> overlappingUtf8Destination(
            reinterpret_cast<char *>(reverseOverlapStorage.data()),
            reverseOverlapStorage.size() * sizeof(char16_t));
        const Unicode::Types::Utf16ToUtf8Result overlapToUtf8 = Unicode::Utf16::convertToUtf8(reverseOverlapStorage, overlappingUtf8Destination);
        static_cast<void>(context.expectEq("UTF-16 to UTF-8 overlap outcome", ConversionOutcome::OverlappingRanges, overlapToUtf8.outcome));
        static_cast<void>(context.expectEq("UTF-16 to UTF-8 overlap consumes nothing", std::size_t{0}, overlapToUtf8.sourceCodeUnitsConsumed));
        static_cast<void>(context.expectEq("UTF-16 to UTF-8 overlap writes nothing", std::size_t{0}, overlapToUtf8.bytesWritten));
    }

    /// @brief Exhaustively verifies scalar round trips and every proper UTF-8 split point.
    void testExhaustiveScalarRoundTrips(TestSupport::Context &context)
    {
        std::size_t testedScalars = 0;
        for (std::uint32_t value = 0; value <= 0x10FFFFU; ++value)
        {
            const char32_t scalar = static_cast<char32_t>(value);
            if (!Unicode::isScalarValue(scalar))
            {
                continue;
            }

            const Unicode::Types::Utf8EncodeResult utf8 = Unicode::Utf8::encodeScalar(scalar);
            if (utf8.outcome != EncodeOutcome::Encoded)
            {
                context.fail("exhaustive UTF-8 encode", std::format("U+{:06X} did not encode", value));
                return;
            }
            const Unicode::Types::Utf8DecodeResult utf8Decoded = Unicode::Utf8::decodeScalar(encodedBytes(utf8));
            if (utf8Decoded.outcome != DecodeOutcome::Decoded || utf8Decoded.scalar != scalar || utf8Decoded.bytesConsumed != utf8.byteCount)
            {
                context.fail("exhaustive UTF-8 round trip", std::format("U+{:06X} did not decode back exactly", value));
                return;
            }

            for (std::size_t split = 1; split < static_cast<std::size_t>(utf8.byteCount); ++split)
            {
                const Unicode::Types::Utf8DecodeResult prefix = Unicode::Utf8::decodeScalar(std::string_view(utf8.bytes.data(), split));
                if (prefix.outcome != DecodeOutcome::Incomplete || prefix.scalar != U'\0' || prefix.bytesConsumed != 0)
                {
                    context.fail(
                        "exhaustive UTF-8 split point",
                        std::format("U+{:06X} split at byte {} did not report deterministic incomplete input", value, split));
                    return;
                }
            }

            const Unicode::Types::Utf16EncodeResult utf16 = Unicode::Utf16::encodeScalar(scalar);
            if (utf16.outcome != EncodeOutcome::Encoded)
            {
                context.fail("exhaustive UTF-16 encode", std::format("U+{:06X} did not encode", value));
                return;
            }
            const Unicode::Types::Utf16DecodeResult utf16Decoded = Unicode::Utf16::decodeScalar(encodedCodeUnits(utf16));
            if (utf16Decoded.outcome != DecodeOutcome::Decoded || utf16Decoded.scalar != scalar ||
                utf16Decoded.codeUnitsConsumed != utf16.codeUnitCount)
            {
                context.fail("exhaustive UTF-16 round trip", std::format("U+{:06X} did not decode back exactly", value));
                return;
            }

            std::array<char16_t, Unicode::Utf16::kMaximumScalarCodeUnits> convertedUtf16{};
            const Unicode::Types::Utf8ToUtf16Result toUtf16 = Unicode::Utf8::convertToUtf16(encodedBytes(utf8), convertedUtf16);
            if (toUtf16.outcome != ConversionOutcome::Converted || toUtf16.sourceBytesConsumed != utf8.byteCount ||
                toUtf16.codeUnitsWritten != utf16.codeUnitCount ||
                !std::equal(utf16.codeUnits.begin(), utf16.codeUnits.begin() + utf16.codeUnitCount, convertedUtf16.begin()))
            {
                context.fail("exhaustive UTF-8 to UTF-16 conversion", std::format("U+{:06X} did not convert exactly", value));
                return;
            }

            std::array<char, Unicode::Utf8::kMaximumScalarBytes> convertedUtf8{};
            const Unicode::Types::Utf16ToUtf8Result toUtf8 = Unicode::Utf16::convertToUtf8(encodedCodeUnits(utf16), convertedUtf8);
            if (toUtf8.outcome != ConversionOutcome::Converted || toUtf8.sourceCodeUnitsConsumed != utf16.codeUnitCount ||
                toUtf8.bytesWritten != utf8.byteCount || !std::equal(utf8.bytes.begin(), utf8.bytes.begin() + utf8.byteCount, convertedUtf8.begin()))
            {
                context.fail("exhaustive UTF-16 to UTF-8 conversion", std::format("U+{:06X} did not convert exactly", value));
                return;
            }

            ++testedScalars;
        }

        static_cast<void>(context.expectEq("exhaustive scalar count", std::size_t{1112064}, testedScalars));
        context.pass("every Unicode scalar round-trips through UTF-8, UTF-16, and both conversion directions");
    }

    /// @brief Verifies code-point traversal endpoints, alignment policy, malformed input, and multibyte offsets.
    void testCodePointBoundaries(TestSupport::Context &context)
    {
        const std::string text = bytes({0x41, 0xC2, 0xA2, 0xE2, 0x82, 0xAC, 0xF0, 0x9F, 0x98, 0x80});
        const std::array<std::size_t, 5> offsets{0, 1, 3, 6, 10};

        const Unicode::Types::Utf8BoundaryResult emptyNext = Unicode::Utf8::nextCodePointBoundary({}, 0);
        const Unicode::Types::Utf8BoundaryResult emptyPrevious = Unicode::Utf8::previousCodePointBoundary({}, 0);
        static_cast<void>(context.expectEq("empty next code-point outcome", BoundaryOutcome::AtEnd, emptyNext.outcome));
        static_cast<void>(context.expectEq("empty previous code-point outcome", BoundaryOutcome::AtBeginning, emptyPrevious.outcome));

        for (std::size_t index = 0; index + 1 < offsets.size(); ++index)
        {
            const Unicode::Types::Utf8BoundaryResult next = Unicode::Utf8::nextCodePointBoundary(text, offsets[index]);
            static_cast<void>(context.expectEq("next code-point boundary outcome", BoundaryOutcome::Found, next.outcome));
            static_cast<void>(context.expectEq("next code-point boundary offset", offsets[index + 1], next.byteOffset));
        }
        const Unicode::Types::Utf8BoundaryResult nextEnd = Unicode::Utf8::nextCodePointBoundary(text, text.size());
        static_cast<void>(context.expectEq("next code-point boundary at end outcome", BoundaryOutcome::AtEnd, nextEnd.outcome));
        static_cast<void>(context.expectEq("next code-point boundary at end offset", text.size(), nextEnd.byteOffset));

        const Unicode::Types::Utf8BoundaryResult previousBeginning = Unicode::Utf8::previousCodePointBoundary(text, 0);
        static_cast<void>(
            context.expectEq("previous code-point boundary at beginning outcome", BoundaryOutcome::AtBeginning, previousBeginning.outcome));
        static_cast<void>(context.expectEq("previous code-point boundary at beginning offset", std::size_t{0}, previousBeginning.byteOffset));
        for (std::size_t index = 1; index < offsets.size(); ++index)
        {
            const Unicode::Types::Utf8BoundaryResult previous = Unicode::Utf8::previousCodePointBoundary(text, offsets[index]);
            static_cast<void>(context.expectEq("previous code-point boundary outcome", BoundaryOutcome::Found, previous.outcome));
            static_cast<void>(context.expectEq("previous code-point boundary offset", offsets[index - 1], previous.byteOffset));
        }

        for (const std::size_t misaligned : {std::size_t{2}, std::size_t{4}, std::size_t{5}, std::size_t{7}, std::size_t{8}, std::size_t{9}})
        {
            const Unicode::Types::Utf8BoundaryResult next = Unicode::Utf8::nextCodePointBoundary(text, misaligned);
            const Unicode::Types::Utf8BoundaryResult previous = Unicode::Utf8::previousCodePointBoundary(text, misaligned);
            static_cast<void>(context.expectEq("misaligned next code-point offset outcome", BoundaryOutcome::InvalidOffset, next.outcome));
            static_cast<void>(context.expectEq("misaligned next code-point preserves offset", misaligned, next.byteOffset));
            static_cast<void>(context.expectEq("misaligned previous code-point offset outcome", BoundaryOutcome::InvalidOffset, previous.outcome));
            static_cast<void>(context.expectEq("misaligned previous code-point preserves offset", misaligned, previous.byteOffset));
        }

        const Unicode::Types::Utf8BoundaryResult beyondNext = Unicode::Utf8::nextCodePointBoundary(text, text.size() + 1);
        const Unicode::Types::Utf8BoundaryResult beyondPrevious = Unicode::Utf8::previousCodePointBoundary(text, text.size() + 1);
        static_cast<void>(context.expectEq("beyond-end next code-point outcome", BoundaryOutcome::InvalidOffset, beyondNext.outcome));
        static_cast<void>(context.expectEq("beyond-end previous code-point outcome", BoundaryOutcome::InvalidOffset, beyondPrevious.outcome));

        const std::string incomplete = bytes({0xC2});
        const Unicode::Types::Utf8BoundaryResult incompleteNext = Unicode::Utf8::nextCodePointBoundary(incomplete, 0);
        const Unicode::Types::Utf8BoundaryResult incompletePrevious = Unicode::Utf8::previousCodePointBoundary(incomplete, 1);
        static_cast<void>(context.expectEq("incomplete next code-point outcome", BoundaryOutcome::InvalidEncoding, incompleteNext.outcome));
        static_cast<void>(context.expectEq("incomplete previous code-point outcome", BoundaryOutcome::InvalidEncoding, incompletePrevious.outcome));

        const std::string malformed = std::string("A") + bytes({0xFF});
        const Unicode::Types::Utf8BoundaryResult malformedNext = Unicode::Utf8::nextCodePointBoundary(malformed, 1);
        const Unicode::Types::Utf8BoundaryResult malformedPrevious = Unicode::Utf8::previousCodePointBoundary(malformed, 2);
        static_cast<void>(context.expectEq("malformed next code-point outcome", BoundaryOutcome::InvalidEncoding, malformedNext.outcome));
        static_cast<void>(context.expectEq("malformed previous code-point outcome", BoundaryOutcome::InvalidEncoding, malformedPrevious.outcome));
    }

    struct GraphemeFixture
    {
        std::string text;
        std::vector<std::size_t> codePointOffsets;
        std::vector<std::size_t> boundaries;
    };

    /// @brief Builds UTF-8 text and expected byte boundaries from explicit grapheme clusters.
    GraphemeFixture graphemeFixture(std::initializer_list<std::initializer_list<char32_t>> clusters)
    {
        GraphemeFixture fixture;
        fixture.codePointOffsets.push_back(0);
        fixture.boundaries.push_back(0);

        for (const auto &cluster : clusters)
        {
            for (const char32_t scalar : cluster)
            {
                const Unicode::Types::Utf8EncodeResult encoded = Unicode::Utf8::encodeScalar(scalar);
                fixture.text.append(encoded.bytes.data(), encoded.byteCount);
                fixture.codePointOffsets.push_back(fixture.text.size());
            }
            fixture.boundaries.push_back(fixture.text.size());
        }
        return fixture;
    }

    /// @brief Returns the expected result of moving forward from one code-point-aligned offset.
    Unicode::Types::Utf8BoundaryResult expectedNextGrapheme(const GraphemeFixture &fixture, std::size_t offset)
    {
        if (offset == fixture.text.size())
        {
            return {.byteOffset = offset, .outcome = BoundaryOutcome::AtEnd};
        }
        const auto boundary = std::upper_bound(fixture.boundaries.begin(), fixture.boundaries.end(), offset);
        return {.byteOffset = *boundary, .outcome = BoundaryOutcome::Found};
    }

    /// @brief Returns the expected result of moving backward from one code-point-aligned offset.
    Unicode::Types::Utf8BoundaryResult expectedPreviousGrapheme(const GraphemeFixture &fixture, std::size_t offset)
    {
        if (offset == 0)
        {
            return {.byteOffset = 0, .outcome = BoundaryOutcome::AtBeginning};
        }
        const auto boundary = std::lower_bound(fixture.boundaries.begin(), fixture.boundaries.end(), offset);
        return {.byteOffset = *(boundary - 1), .outcome = BoundaryOutcome::Found};
    }

    /// @brief Returns a diagnostic when one fixture disagrees with stateless or indexed traversal.
    std::optional<std::string> graphemeFailure(const GraphemeFixture &fixture)
    {
        for (const std::size_t offset : fixture.codePointOffsets)
        {
            const Unicode::Types::Utf8BoundaryResult expectedNext = expectedNextGrapheme(fixture, offset);
            const Unicode::Types::Utf8BoundaryResult actualNext = Unicode::Utf8::nextGraphemeBoundary(fixture.text, offset);
            if (actualNext.outcome != expectedNext.outcome || actualNext.byteOffset != expectedNext.byteOffset)
            {
                return std::format(
                    "next offset {} expected outcome={} offset={} but got outcome={} offset={}",
                    offset,
                    static_cast<unsigned int>(expectedNext.outcome),
                    expectedNext.byteOffset,
                    static_cast<unsigned int>(actualNext.outcome),
                    actualNext.byteOffset);
            }

            const Unicode::Types::Utf8BoundaryResult expectedPrevious = expectedPreviousGrapheme(fixture, offset);
            const Unicode::Types::Utf8BoundaryResult actualPrevious = Unicode::Utf8::previousGraphemeBoundary(fixture.text, offset);
            if (actualPrevious.outcome != expectedPrevious.outcome || actualPrevious.byteOffset != expectedPrevious.byteOffset)
            {
                return std::format(
                    "previous offset {} expected outcome={} offset={} but got outcome={} offset={}",
                    offset,
                    static_cast<unsigned int>(expectedPrevious.outcome),
                    expectedPrevious.byteOffset,
                    static_cast<unsigned int>(actualPrevious.outcome),
                    actualPrevious.byteOffset);
            }
        }

        std::vector<std::size_t> boundaryStorage(fixture.codePointOffsets.size());
        Unicode::Utf8::GraphemeCursor cursor;
        const Unicode::Types::Utf8GraphemeIndexResult indexed = cursor.reset(fixture.text, boundaryStorage);
        if (indexed.outcome != Unicode::Types::GraphemeIndexOutcome::Indexed || indexed.requiredBoundaryCount != fixture.boundaries.size() ||
            cursor.boundaryCount() != fixture.boundaries.size())
        {
            return std::format(
                "cursor index expected boundaries={} but got outcome={} required={} retained={}",
                fixture.boundaries.size(),
                static_cast<unsigned int>(indexed.outcome),
                indexed.requiredBoundaryCount,
                cursor.boundaryCount());
        }

        for (std::size_t index = 1; index < fixture.boundaries.size(); ++index)
        {
            const Unicode::Types::Utf8BoundaryResult actual = cursor.next();
            if (actual.outcome != BoundaryOutcome::Found || actual.byteOffset != fixture.boundaries[index])
            {
                return std::format(
                    "cursor next index {} expected offset={} but got outcome={} offset={}",
                    index,
                    fixture.boundaries[index],
                    static_cast<unsigned int>(actual.outcome),
                    actual.byteOffset);
            }
        }

        const Unicode::Types::Utf8BoundaryResult atEnd = cursor.next();
        if (atEnd.outcome != BoundaryOutcome::AtEnd || atEnd.byteOffset != fixture.text.size())
        {
            return std::format(
                "cursor end expected offset={} but got outcome={} offset={}",
                fixture.text.size(),
                static_cast<unsigned int>(atEnd.outcome),
                atEnd.byteOffset);
        }

        for (std::size_t index = fixture.boundaries.size() - 1; index > 0; --index)
        {
            const Unicode::Types::Utf8BoundaryResult actual = cursor.previous();
            if (actual.outcome != BoundaryOutcome::Found || actual.byteOffset != fixture.boundaries[index - 1])
            {
                return std::format(
                    "cursor previous index {} expected offset={} but got outcome={} offset={}",
                    index,
                    fixture.boundaries[index - 1],
                    static_cast<unsigned int>(actual.outcome),
                    actual.byteOffset);
            }
        }

        const Unicode::Types::Utf8BoundaryResult atBeginning = cursor.previous();
        if (atBeginning.outcome != BoundaryOutcome::AtBeginning || atBeginning.byteOffset != 0)
        {
            return std::format(
                "cursor beginning expected offset=0 but got outcome={} offset={}",
                static_cast<unsigned int>(atBeginning.outcome),
                atBeginning.byteOffset);
        }

        return std::nullopt;
    }

    /// @brief Verifies one targeted grapheme fixture in both directions and from inside clusters.
    void expectGraphemeFixture(TestSupport::Context &context, std::string_view name, const GraphemeFixture &fixture)
    {
        const std::optional<std::string> failure = graphemeFailure(fixture);
        if (failure.has_value())
        {
            context.fail(name, *failure);
        }
        else
        {
            context.pass(name);
        }
    }

    /// @brief Verifies Unicode 17 grapheme rules with targeted rule-oriented examples and error paths.
    void testTargetedGraphemeBoundaries(TestSupport::Context &context)
    {
        expectGraphemeFixture(context, "ASCII grapheme boundaries", graphemeFixture({{U'A'}, {U'B'}, {U'C'}}));
        expectGraphemeFixture(context, "CR LF grapheme pair", graphemeFixture({{U'\r', U'\n'}, {U'X'}}));
        expectGraphemeFixture(context, "combining mark grapheme", graphemeFixture({{U'a', static_cast<char32_t>(0x0308)}, {U'b'}}));
        expectGraphemeFixture(
            context,
            "Hangul L V T grapheme",
            graphemeFixture({{static_cast<char32_t>(0x1100), static_cast<char32_t>(0x1161), static_cast<char32_t>(0x11A8)}}));
        expectGraphemeFixture(context, "prepending grapheme property", graphemeFixture({{static_cast<char32_t>(0x0600), U'A'}}));
        expectGraphemeFixture(
            context,
            "spacing mark grapheme property",
            graphemeFixture({{static_cast<char32_t>(0x0915), static_cast<char32_t>(0x093E)}}));
        expectGraphemeFixture(
            context,
            "Indic conjunct GB9c",
            graphemeFixture({{static_cast<char32_t>(0x0915), static_cast<char32_t>(0x094D), static_cast<char32_t>(0x0915)}}));
        expectGraphemeFixture(
            context,
            "emoji ZWJ GB11",
            graphemeFixture({{static_cast<char32_t>(0x1F469), static_cast<char32_t>(0x200D), static_cast<char32_t>(0x1F4BB)}}));
        expectGraphemeFixture(
            context,
            "regional indicator pairing",
            graphemeFixture({
                {static_cast<char32_t>(0x1F1FA), static_cast<char32_t>(0x1F1F8)},
                {static_cast<char32_t>(0x1F1E8)},
            }));
        expectGraphemeFixture(context, "emoji modifier extend", graphemeFixture({{static_cast<char32_t>(0x1F44D), static_cast<char32_t>(0x1F3FD)}}));

        const Unicode::Types::Utf8BoundaryResult emptyNext = Unicode::Utf8::nextGraphemeBoundary({}, 0);
        const Unicode::Types::Utf8BoundaryResult emptyPrevious = Unicode::Utf8::previousGraphemeBoundary({}, 0);
        static_cast<void>(context.expectEq("empty next grapheme outcome", BoundaryOutcome::AtEnd, emptyNext.outcome));
        static_cast<void>(context.expectEq("empty previous grapheme outcome", BoundaryOutcome::AtBeginning, emptyPrevious.outcome));

        const std::string multibyte = bytes({0xC2, 0xA2});
        const Unicode::Types::Utf8BoundaryResult misalignedNext = Unicode::Utf8::nextGraphemeBoundary(multibyte, 1);
        const Unicode::Types::Utf8BoundaryResult misalignedPrevious = Unicode::Utf8::previousGraphemeBoundary(multibyte, 1);
        static_cast<void>(context.expectEq("misaligned next grapheme outcome", BoundaryOutcome::InvalidOffset, misalignedNext.outcome));
        static_cast<void>(context.expectEq("misaligned previous grapheme outcome", BoundaryOutcome::InvalidOffset, misalignedPrevious.outcome));

        const Unicode::Types::Utf8BoundaryResult beyondNext = Unicode::Utf8::nextGraphemeBoundary(multibyte, multibyte.size() + 1);
        const Unicode::Types::Utf8BoundaryResult beyondPrevious = Unicode::Utf8::previousGraphemeBoundary(multibyte, multibyte.size() + 1);
        static_cast<void>(context.expectEq("beyond-end next grapheme outcome", BoundaryOutcome::InvalidOffset, beyondNext.outcome));
        static_cast<void>(context.expectEq("beyond-end previous grapheme outcome", BoundaryOutcome::InvalidOffset, beyondPrevious.outcome));

        const std::string malformed = bytes({0xE2, 0x82});
        const Unicode::Types::Utf8BoundaryResult malformedNext = Unicode::Utf8::nextGraphemeBoundary(malformed, 0);
        const Unicode::Types::Utf8BoundaryResult malformedPrevious = Unicode::Utf8::previousGraphemeBoundary(malformed, malformed.size());
        static_cast<void>(context.expectEq("malformed next grapheme outcome", BoundaryOutcome::InvalidEncoding, malformedNext.outcome));
        static_cast<void>(context.expectEq("malformed previous grapheme outcome", BoundaryOutcome::InvalidEncoding, malformedPrevious.outcome));

        const GraphemeFixture cursorFixture = graphemeFixture({
            {U'a', static_cast<char32_t>(0x0308)},
            {static_cast<char32_t>(0x1F469), static_cast<char32_t>(0x200D), static_cast<char32_t>(0x1F4BB)},
            {static_cast<char32_t>(0x0915), static_cast<char32_t>(0x094D), static_cast<char32_t>(0x0915)},
            {U'Z'},
        });

        std::array<std::size_t, 3> shortBoundaryStorage{};
        Unicode::Utf8::GraphemeCursor cursor;
        const Unicode::Types::Utf8GraphemeIndexResult tooSmall = cursor.reset(cursorFixture.text, shortBoundaryStorage);
        static_cast<void>(
            context.expectEq("grapheme cursor short storage outcome", Unicode::Types::GraphemeIndexOutcome::DestinationTooSmall, tooSmall.outcome));
        static_cast<void>(
            context.expectEq("grapheme cursor required boundary count", cursorFixture.boundaries.size(), tooSmall.requiredBoundaryCount));
        static_cast<void>(context.expectFalse("grapheme cursor short storage remains unready", cursor.isReady()));

        std::array<std::size_t, 8> boundaryStorage{};
        const Unicode::Types::Utf8GraphemeIndexResult ready = cursor.reset(cursorFixture.text, boundaryStorage);
        static_cast<void>(context.expectEq("grapheme cursor reset outcome", Unicode::Types::GraphemeIndexOutcome::Indexed, ready.outcome));
        static_cast<void>(context.expectTrue("grapheme cursor becomes ready", cursor.isReady()));
        static_cast<void>(context.expectEq("grapheme cursor starts at zero", std::size_t{0}, cursor.byteOffset()));
        static_cast<void>(context.expectEq("grapheme cursor indexed count", cursorFixture.boundaries.size(), cursor.boundaryCount()));

        const std::size_t insideCombiningCluster = cursorFixture.codePointOffsets[1];
        static_cast<void>(context.expectEq(
            "grapheme cursor seek rejects non-boundary code-point offset",
            BoundaryOutcome::InvalidOffset,
            cursor.seek(insideCombiningCluster).outcome));
        static_cast<void>(context.expectEq("failed grapheme cursor seek preserves position", std::size_t{0}, cursor.byteOffset()));

        static_cast<void>(
            context.expectEq("grapheme cursor seek exact boundary", BoundaryOutcome::Found, cursor.seek(cursorFixture.boundaries[2]).outcome));
        cursor.discardAfterCurrent();
        static_cast<void>(context.expectEq("grapheme cursor discard retained count", std::size_t{3}, cursor.boundaryCount()));
        static_cast<void>(context.expectEq("grapheme cursor discard makes current end", BoundaryOutcome::AtEnd, cursor.next().outcome));
        static_cast<void>(context.expectEq("grapheme cursor can move backward after discard", BoundaryOutcome::Found, cursor.previous().outcome));

        const Unicode::Types::Utf8GraphemeIndexResult invalidCursor = cursor.reset(malformed, boundaryStorage);
        static_cast<void>(
            context.expectEq("grapheme cursor malformed outcome", Unicode::Types::GraphemeIndexOutcome::InvalidEncoding, invalidCursor.outcome));
        static_cast<void>(context.expectEq("grapheme cursor malformed required count", std::size_t{0}, invalidCursor.requiredBoundaryCount));
        static_cast<void>(context.expectFalse("grapheme cursor malformed reset clears state", cursor.isReady()));

        std::array<std::size_t, 1> emptyStorage{};
        const Unicode::Types::Utf8GraphemeIndexResult emptyCursor = cursor.reset({}, emptyStorage);
        static_cast<void>(context.expectEq("empty grapheme cursor outcome", Unicode::Types::GraphemeIndexOutcome::Indexed, emptyCursor.outcome));
        static_cast<void>(context.expectEq("empty grapheme cursor required count", std::size_t{1}, emptyCursor.requiredBoundaryCount));
        static_cast<void>(context.expectEq("empty grapheme cursor boundary count", std::size_t{1}, cursor.boundaryCount()));
        static_cast<void>(context.expectEq("empty grapheme cursor next outcome", BoundaryOutcome::AtEnd, cursor.next().outcome));
        static_cast<void>(context.expectEq("empty grapheme cursor previous outcome", BoundaryOutcome::AtBeginning, cursor.previous().outcome));
    }

    /// @brief Verifies generated-table invariants and representative algorithmic/property classifications.
    void testGeneratedPropertyData(TestSupport::Context &context)
    {
        namespace Generated = Unicode::Internal::Generated;
        using GraphemeBreakClass = Unicode::Internal::GraphemeBreakClass;
        using IndicConjunctBreakClass = Unicode::Internal::IndicConjunctBreakClass;

        static_cast<void>(context.expectEq("generated major version", std::uint8_t{17}, Generated::kUnicodeVersionMajor));
        static_cast<void>(context.expectEq("generated minor version", std::uint8_t{0}, Generated::kUnicodeVersionMinor));
        static_cast<void>(context.expectEq("generated patch version", std::uint8_t{0}, Generated::kUnicodeVersionPatch));
        static_cast<void>(context.expectEq("generated block size", std::size_t{1} << Generated::kBlockShift, Generated::kBlockSize));
        static_cast<void>(context.expectEq("generated block mask", Generated::kBlockSize - 1, Generated::kBlockMask));
        static_cast<void>(context.expectEq(
            "generated index coverage",
            static_cast<std::size_t>(Generated::kHighStart) >> Generated::kBlockShift,
            Generated::kBlockIndexes.size()));
        static_cast<void>(
            context.expectEq("generated property block alignment", std::size_t{0}, Generated::kPropertyBlocks.size() % Generated::kBlockSize));

        const std::size_t blockCount = Generated::kPropertyBlocks.size() / Generated::kBlockSize;
        bool indexesValid = true;
        for (const Generated::BlockIndex blockIndex : Generated::kBlockIndexes)
        {
            if (static_cast<std::size_t>(blockIndex) >= blockCount)
            {
                indexesValid = false;
                break;
            }
        }
        static_cast<void>(context.expectTrue("every generated block index is in range", indexesValid));

        bool packedValuesValid = true;
        for (const std::uint8_t packed : Generated::kPropertyBlocks)
        {
            if ((packed & 0x80U) != 0 || (packed & 0x0FU) > 13U)
            {
                packedValuesValid = false;
                break;
            }
        }
        static_cast<void>(context.expectTrue("every generated packed property value uses the defined bit layout", packedValuesValid));

        const Unicode::Internal::UnicodeProperties carriageReturn = Unicode::Internal::unicodeProperties(U'\r');
        const Unicode::Internal::UnicodeProperties lineFeed = Unicode::Internal::unicodeProperties(U'\n');
        const Unicode::Internal::UnicodeProperties asciiControl = Unicode::Internal::unicodeProperties(U'\0');
        const Unicode::Internal::UnicodeProperties asciiOther = Unicode::Internal::unicodeProperties(U'A');
        const Unicode::Internal::UnicodeProperties hangulLv = Unicode::Internal::unicodeProperties(static_cast<char32_t>(0xAC00));
        const Unicode::Internal::UnicodeProperties hangulLvt = Unicode::Internal::unicodeProperties(static_cast<char32_t>(0xAC01));
        const Unicode::Internal::UnicodeProperties combining = Unicode::Internal::unicodeProperties(static_cast<char32_t>(0x0308));
        const Unicode::Internal::UnicodeProperties zwj = Unicode::Internal::unicodeProperties(static_cast<char32_t>(0x200D));
        const Unicode::Internal::UnicodeProperties regionalIndicator = Unicode::Internal::unicodeProperties(static_cast<char32_t>(0x1F1E6));
        const Unicode::Internal::UnicodeProperties indicConsonant = Unicode::Internal::unicodeProperties(static_cast<char32_t>(0x0915));
        const Unicode::Internal::UnicodeProperties indicLinker = Unicode::Internal::unicodeProperties(static_cast<char32_t>(0x094D));
        const Unicode::Internal::UnicodeProperties pictographic = Unicode::Internal::unicodeProperties(static_cast<char32_t>(0x1F469));

        static_cast<void>(context.expectEq("ASCII CR classification", GraphemeBreakClass::CR, carriageReturn.graphemeBreak));
        static_cast<void>(context.expectEq("ASCII LF classification", GraphemeBreakClass::LF, lineFeed.graphemeBreak));
        static_cast<void>(context.expectEq("ASCII control classification", GraphemeBreakClass::Control, asciiControl.graphemeBreak));
        static_cast<void>(context.expectEq("ASCII ordinary classification", GraphemeBreakClass::Other, asciiOther.graphemeBreak));
        static_cast<void>(context.expectEq("algorithmic Hangul LV classification", GraphemeBreakClass::LV, hangulLv.graphemeBreak));
        static_cast<void>(context.expectEq("algorithmic Hangul LVT classification", GraphemeBreakClass::LVT, hangulLvt.graphemeBreak));
        static_cast<void>(context.expectEq("generated Extend classification", GraphemeBreakClass::Extend, combining.graphemeBreak));
        static_cast<void>(context.expectEq("generated ZWJ classification", GraphemeBreakClass::ZWJ, zwj.graphemeBreak));
        static_cast<void>(context.expectEq("generated RI classification", GraphemeBreakClass::RegionalIndicator, regionalIndicator.graphemeBreak));
        static_cast<void>(
            context.expectEq("generated InCB consonant classification", IndicConjunctBreakClass::Consonant, indicConsonant.indicConjunctBreak));
        static_cast<void>(context.expectEq("generated InCB linker classification", IndicConjunctBreakClass::Linker, indicLinker.indicConjunctBreak));
        static_cast<void>(context.expectTrue("generated Extended_Pictographic classification", pictographic.extendedPictographic));
    }

    /// @brief Resolves the official Unicode 17 grapheme conformance file from explicit or GameWIP cache paths.
    std::optional<std::filesystem::path> findGraphemeBreakTestFile()
    {
        constexpr std::string_view relativePath = "17.0.0/ucd/auxiliary/GraphemeBreakTest.txt";

        if (const char *explicitPath = std::getenv("GAMEWIP_UNICODE_GRAPHEME_BREAK_TEST"); explicitPath != nullptr && *explicitPath != '\0')
        {
            const std::filesystem::path path(explicitPath);
            if (std::filesystem::is_regular_file(path))
            {
                return path;
            }
        }

        if (const char *cacheRoot = std::getenv("GAMEWIP_UNICODE_DATA_ROOT"); cacheRoot != nullptr && *cacheRoot != '\0')
        {
            const std::filesystem::path path = std::filesystem::path(cacheRoot) / relativePath;
            if (std::filesystem::is_regular_file(path))
            {
                return path;
            }
        }

        std::error_code currentPathError;
        std::filesystem::path searchRoot = std::filesystem::current_path(currentPathError);
        if (currentPathError)
        {
            return std::nullopt;
        }

        for (std::size_t depth = 0; depth < 8; ++depth)
        {
            const std::filesystem::path path = searchRoot / "build" / "unicode-data" / relativePath;
            if (std::filesystem::is_regular_file(path))
            {
                return path;
            }
            if (!searchRoot.has_parent_path() || searchRoot.parent_path() == searchRoot)
            {
                break;
            }
            searchRoot = searchRoot.parent_path();
        }
        return std::nullopt;
    }

    /// @brief Parses one official GraphemeBreakTest case into UTF-8 text and expected byte boundaries.
    std::optional<GraphemeFixture> parseGraphemeBreakCase(std::string_view payload, std::string &error)
    {
        constexpr std::string_view breakMarker = "\xC3\xB7";
        constexpr std::string_view noBreakMarker = "\xC3\x97";

        std::istringstream tokens{std::string(payload)};
        std::string marker;
        if (!(tokens >> marker))
        {
            return std::nullopt;
        }
        if (marker != breakMarker)
        {
            error = "case does not begin with a break marker";
            return std::nullopt;
        }

        GraphemeFixture fixture;
        fixture.codePointOffsets.push_back(0);
        fixture.boundaries.push_back(0);

        std::string scalarText;
        while (tokens >> scalarText)
        {
            std::uint32_t scalarValue = 0;
            const char *begin = scalarText.data();
            const char *end = scalarText.data() + scalarText.size();
            const auto parsed = std::from_chars(begin, end, scalarValue, 16);
            if (parsed.ec != std::errc{} || parsed.ptr != end || scalarValue > 0x10FFFFU)
            {
                error = std::format("invalid scalar token '{}'", scalarText);
                return std::nullopt;
            }

            const char32_t scalar = static_cast<char32_t>(scalarValue);
            const Unicode::Types::Utf8EncodeResult encoded = Unicode::Utf8::encodeScalar(scalar);
            if (encoded.outcome != EncodeOutcome::Encoded)
            {
                error = std::format("non-scalar token '{}'", scalarText);
                return std::nullopt;
            }
            fixture.text.append(encoded.bytes.data(), encoded.byteCount);
            fixture.codePointOffsets.push_back(fixture.text.size());

            if (!(tokens >> marker))
            {
                error = "case ends without a boundary marker";
                return std::nullopt;
            }
            if (marker == breakMarker)
            {
                fixture.boundaries.push_back(fixture.text.size());
            }
            else if (marker != noBreakMarker)
            {
                error = std::format("unknown boundary marker '{}'", marker);
                return std::nullopt;
            }
        }

        if (fixture.codePointOffsets.size() == 1 || fixture.boundaries.empty() || fixture.boundaries.back() != fixture.text.size())
        {
            error = "case is empty or does not end with a break marker";
            return std::nullopt;
        }
        return fixture;
    }

    /// @brief Executes the official Unicode 17 GraphemeBreakTest cases when versioned source data is available.
    void testOfficialGraphemeConformance(TestSupport::Context &context)
    {
        const std::optional<std::filesystem::path> conformancePath = findGraphemeBreakTestFile();
        if (!conformancePath.has_value())
        {
            constexpr std::string_view reason =
                "Unicode 17.0.0 GraphemeBreakTest.txt is not cached. Run '.\\gamewip.bat unicode -UnicodeAction verify' "
                "or set GAMEWIP_UNICODE_GRAPHEME_BREAK_TEST.";
            if (std::getenv("GAMEWIP_REQUIRE_UNICODE_CONFORMANCE_TESTS") != nullptr)
            {
                context.fail("official Unicode grapheme conformance data", reason);
            }
            else
            {
                context.skip("official Unicode grapheme conformance data", reason);
            }
            return;
        }

        std::ifstream input(*conformancePath, std::ios::binary);
        if (!input)
        {
            context.fail("official Unicode grapheme conformance data", std::format("could not open {}", conformancePath->string()));
            return;
        }

        bool versionConfirmed = false;
        std::size_t lineNumber = 0;
        std::size_t caseCount = 0;
        std::size_t checkedOffsets = 0;
        std::string line;
        while (std::getline(input, line))
        {
            ++lineNumber;
            if (line.find("GraphemeBreakTest-17.0.0.txt") != std::string::npos)
            {
                versionConfirmed = true;
            }

            const std::size_t comment = line.find('#');
            const std::string_view payload(line.data(), comment == std::string::npos ? line.size() : comment);
            if (payload.find_first_not_of(" \t\r\n") == std::string_view::npos)
            {
                continue;
            }

            std::string parseError;
            const std::optional<GraphemeFixture> fixture = parseGraphemeBreakCase(payload, parseError);
            if (!fixture.has_value())
            {
                context.fail(
                    "official Unicode grapheme conformance parsing",
                    std::format("{}:{}: {}", conformancePath->string(), lineNumber, parseError));
                return;
            }

            const std::optional<std::string> failure = graphemeFailure(*fixture);
            if (failure.has_value())
            {
                context.fail("official Unicode grapheme conformance", std::format("{}:{}: {}", conformancePath->string(), lineNumber, *failure));
                return;
            }

            ++caseCount;
            checkedOffsets += fixture->codePointOffsets.size();
        }

        if (!input.eof())
        {
            context.fail("official Unicode grapheme conformance data", std::format("read failed before EOF: {}", conformancePath->string()));
            return;
        }
        if (!versionConfirmed)
        {
            context.fail("official Unicode grapheme conformance data", "file does not declare GraphemeBreakTest-17.0.0.txt");
            return;
        }
        if (caseCount == 0)
        {
            context.fail("official Unicode grapheme conformance data", "file contained no test cases");
            return;
        }

        context.info(std::format("Unicode grapheme conformance source: {}", conformancePath->string()));
        context.info(std::format("Unicode grapheme conformance cases={} code-point offsets={} directions=2", caseCount, checkedOffsets));
        context.pass("all official Unicode 17.0.0 GraphemeBreakTest cases pass forward and backward traversal");
    }
} // namespace

namespace GameWIP::Test
{
    int runUnicodeTests(int, char **, const UnicodeTestOptions &options)
    {
        TestSupport::Types::ReportOptions reportOptions;
        reportOptions.writeConsole = true;
        reportOptions.consoleVerbosity =
            options.verboseConsole ? TestSupport::Types::ConsoleVerbosity::Full : TestSupport::Types::ConsoleVerbosity::Minimal;
        reportOptions.writeReport = options.writeReport;
        reportOptions.appendReport = options.appendReport;
        reportOptions.reportPath = options.reportPath;

        TestSupport::Runner runner(reportOptions);
        runner.info("Unicode library target: strict UTF-8/UTF-16 primitives and Unicode 17.0.0 extended grapheme boundaries");

        runner.runSuite("Unicode version and scalar predicates", testVersionAndScalarPredicates);
        runner.runSuite("Unicode UTF-8 codec and validation", testUtf8CodecAndValidation);
        runner.runSuite("Unicode UTF-16 codec and validation", testUtf16CodecAndValidation);
        runner.runSuite("Unicode conversions", testConversions);
        runner.runSuite("Unicode exhaustive scalar round trips", testExhaustiveScalarRoundTrips);
        runner.runSuite("Unicode code-point boundaries", testCodePointBoundaries);
        runner.runSuite("Unicode targeted grapheme boundaries", testTargetedGraphemeBoundaries);
        runner.runSuite("Unicode generated property data", testGeneratedPropertyData);
        runner.runSuite("Unicode official grapheme conformance", testOfficialGraphemeConformance);

        const TestSupport::Types::Summary result = runner.result();
        runner.summary(std::format("Unicode library self-tests passed={} failed={} skipped={}", result.passed, result.failed, result.skipped));
        return runner.exitCode();
    }
} // namespace GameWIP::Test
