/// @file utf8_test.inl
/// @brief Focused unicode utf8 correctness suites.

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
    static_cast<void>(context.expectEq("incomplete UTF-8 measurement source progress", std::size_t{1}, incompleteUtf8Measure.sourceBytesProcessed));
    static_cast<void>(context.expectEq("incomplete UTF-8 measurement destination progress", std::size_t{1}, incompleteUtf8Measure.requiredCodeUnits));

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
    static_cast<void>(context.expectEq("incomplete UTF-16 measurement destination progress", std::size_t{1}, incompleteUtf16Measure.requiredBytes));

    const std::array<char16_t, 3> invalidUtf16{u'A', static_cast<char16_t>(0xD800), u'B'};
    const Unicode::Types::Utf16ToUtf8MeasureResult invalidUtf16Measure = Unicode::Utf16::measureToUtf8(invalidUtf16);
    static_cast<void>(context.expectEq("invalid UTF-16 measurement outcome", MeasureOutcome::InvalidEncoding, invalidUtf16Measure.outcome));
    static_cast<void>(context.expectEq("invalid UTF-16 measurement source progress", std::size_t{1}, invalidUtf16Measure.sourceCodeUnitsProcessed));
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
    static_cast<void>(context.expectEq("incomplete UTF-8 conversion source progress", std::size_t{1}, incompleteUtf8Conversion.sourceBytesConsumed));
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
    static_cast<void>(context.expectEq("incomplete UTF-16 conversion destination progress", std::size_t{1}, incompleteUtf16Conversion.bytesWritten));
    static_cast<void>(
        context.expectEq("incomplete UTF-16 conversion preserves destination after prefix", static_cast<char>(0x33), incompleteUtf8Destination[1]));

    std::array<char16_t, 4> malformedUtf16Destination;
    malformedUtf16Destination.fill(static_cast<char16_t>(0x4444));
    const Unicode::Types::Utf8ToUtf16Result invalidUtf8Conversion = Unicode::Utf8::convertToUtf16(invalidUtf8, malformedUtf16Destination);
    static_cast<void>(context.expectEq("invalid UTF-8 conversion outcome", ConversionOutcome::InvalidEncoding, invalidUtf8Conversion.outcome));
    static_cast<void>(context.expectEq("invalid UTF-8 conversion source progress", std::size_t{1}, invalidUtf8Conversion.sourceBytesConsumed));
    static_cast<void>(context.expectEq("invalid UTF-8 conversion destination progress", std::size_t{1}, invalidUtf8Conversion.codeUnitsWritten));
    static_cast<void>(
        context.expectEq("invalid UTF-8 conversion preserves destination after prefix", static_cast<char16_t>(0x4444), malformedUtf16Destination[1]));

    std::array<char, 8> malformedUtf8Destination;
    malformedUtf8Destination.fill(static_cast<char>(0x44));
    const Unicode::Types::Utf16ToUtf8Result invalidUtf16Conversion = Unicode::Utf16::convertToUtf8(invalidUtf16, malformedUtf8Destination);
    static_cast<void>(context.expectEq("invalid UTF-16 conversion outcome", ConversionOutcome::InvalidEncoding, invalidUtf16Conversion.outcome));
    static_cast<void>(context.expectEq("invalid UTF-16 conversion source progress", std::size_t{1}, invalidUtf16Conversion.sourceCodeUnitsConsumed));
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
        if (utf16Decoded.outcome != DecodeOutcome::Decoded || utf16Decoded.scalar != scalar || utf16Decoded.codeUnitsConsumed != utf16.codeUnitCount)
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
    static_cast<void>(context.expectEq("previous code-point boundary at beginning outcome", BoundaryOutcome::AtBeginning, previousBeginning.outcome));
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
