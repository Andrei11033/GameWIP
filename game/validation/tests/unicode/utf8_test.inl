/// @file utf8_test.inl
/// @brief Focused Unicode UTF-8 tests.

/// ------------------------------------------------------------
/// Scalar values and UTF-8 codec
/// ------------------------------------------------------------

/// @brief Verifies standard-version reporting and scalar/surrogate classification boundaries.
void testVersionAndScalarPredicates(TestSupport::Context &context)
{
    // Confirm the Unicode data version.
    const Unicode::Types::Version version = Unicode::getStandardVersion();
    static_cast<void>(context.expectEq("Unicode major version", std::uint8_t{17}, version.major));
    static_cast<void>(context.expectEq("Unicode minor version", std::uint8_t{0}, version.minor));
    static_cast<void>(context.expectEq("Unicode patch version", std::uint8_t{0}, version.patch));

    // Check scalar and surrogate boundaries.
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
    // Decode valid scalar boundaries.
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

    // Distinguish incomplete prefixes from malformed input.
    for(const std::string &input : {
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

    for(const std::string &input : {
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
        std::string name = "strict-invalid UTF-8";

        for(const char value : input)
        {
            name += std::format(" {:02X}", static_cast<unsigned char>(value));
        }
        expectUtf8Decode(context, name, input, DecodeOutcome::InvalidEncoding);
    }

    expectUtf8Decode(context, "decoder stops after first scalar", bytes({0x41, 0xFF}), DecodeOutcome::Decoded, U'A', 1);

    // Encode representative scalar boundaries.
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

    for(const EncodeCase &testCase : encodeCases)
    {
        const std::string name = std::format("UTF-8 encode U+{:04X}", static_cast<std::uint32_t>(testCase.scalar));
        const Unicode::Types::Utf8::EncodeResult encoded = Unicode::Utf8::encodeScalar(testCase.scalar);
        static_cast<void>(context.expectEq(name + " outcome", EncodeOutcome::Encoded, encoded.outcome));
        static_cast<void>(context.expectEq(name + " byte count", testCase.expected.size(), static_cast<std::size_t>(encoded.byteCount)));
        static_cast<void>(context.expectEq(name + " bytes", std::string_view(testCase.expected), encodedBytes(encoded)));
    }

    for(const char32_t invalidScalar : {
            static_cast<char32_t>(0xD800),
            static_cast<char32_t>(0xDFFF),
            static_cast<char32_t>(0x110000),
        })
    {
        const std::string name = std::format("invalid scalar UTF-8 U+{:04X}", static_cast<std::uint32_t>(invalidScalar));
        const Unicode::Types::Utf8::EncodeResult encoded = Unicode::Utf8::encodeScalar(invalidScalar);
        static_cast<void>(context.expectEq(name + " outcome", EncodeOutcome::InvalidScalar, encoded.outcome));
        static_cast<void>(context.expectEq(name + " byte count", std::uint8_t{0}, encoded.byteCount));
        static_cast<void>(context.expectTrue(
            name + " storage remains zero",
            std::ranges::all_of(
                encoded.bytes,
                [](char value)
                {
                    return value == '\0';
                })));
    }

    // Validate complete ranges and report the valid prefix.
    const std::string validText = bytes({0x00, 0x41, 0xC2, 0x80, 0xE2, 0x82, 0xAC, 0xF0, 0x9F, 0x98, 0x80});
    const Unicode::Types::Utf8::ValidationResult valid = Unicode::Utf8::validate(validText);
    static_cast<void>(context.expectEq("valid UTF-8 range outcome", ValidationOutcome::Valid, valid.outcome));
    static_cast<void>(context.expectEq("valid UTF-8 range prefix", validText.size(), valid.validPrefixBytes));

    const Unicode::Types::Utf8::ValidationResult empty = Unicode::Utf8::validate({});
    static_cast<void>(context.expectEq("empty UTF-8 range outcome", ValidationOutcome::Valid, empty.outcome));
    static_cast<void>(context.expectEq("empty UTF-8 range prefix", std::size_t{0}, empty.validPrefixBytes));

    const std::string incompleteText = std::string("A") + bytes({0xE2, 0x82});
    const Unicode::Types::Utf8::ValidationResult incomplete = Unicode::Utf8::validate(incompleteText);
    static_cast<void>(context.expectEq("incomplete UTF-8 validation outcome", ValidationOutcome::Incomplete, incomplete.outcome));
    static_cast<void>(context.expectEq("incomplete UTF-8 validation prefix", std::size_t{1}, incomplete.validPrefixBytes));

    const std::string invalidText = std::string("A") + bytes({0xE2, 0x28, 0xA1});
    const Unicode::Types::Utf8::ValidationResult invalid = Unicode::Utf8::validate(invalidText);
    static_cast<void>(context.expectEq("invalid UTF-8 validation outcome", ValidationOutcome::InvalidEncoding, invalid.outcome));
    static_cast<void>(context.expectEq("invalid UTF-8 validation prefix", std::size_t{1}, invalid.validPrefixBytes));
}
