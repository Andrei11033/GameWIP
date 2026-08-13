/// @file utf16_test.inl
/// @brief Focused unicode utf16 correctness suites.

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
