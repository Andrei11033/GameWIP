/// @file conversion_test.inl
/// @brief Focused UTF-8 and UTF-16 conversion tests.

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

    // Measure valid, empty, incomplete, and malformed input.
    const Unicode::Types::Utf8::ToUtf16MeasureResult toUtf16Measure = Unicode::Utf8::measureToUtf16(utf8);
    static_cast<void>(context.expectEq("UTF-8 to UTF-16 measurement outcome", MeasureOutcome::Measured, toUtf16Measure.outcome));
    static_cast<void>(context.expectEq("UTF-8 to UTF-16 measurement source", utf8.size(), toUtf16Measure.sourceBytesProcessed));
    static_cast<void>(context.expectEq("UTF-8 to UTF-16 measurement destination", utf16.size(), toUtf16Measure.requiredCodeUnits));

    const Unicode::Types::Utf16::ToUtf8MeasureResult toUtf8Measure = Unicode::Utf16::measureToUtf8(utf16);
    static_cast<void>(context.expectEq("UTF-16 to UTF-8 measurement outcome", MeasureOutcome::Measured, toUtf8Measure.outcome));
    static_cast<void>(context.expectEq("UTF-16 to UTF-8 measurement source", utf16.size(), toUtf8Measure.sourceCodeUnitsProcessed));
    static_cast<void>(context.expectEq("UTF-16 to UTF-8 measurement destination", utf8.size(), toUtf8Measure.requiredBytes));

    const Unicode::Types::Utf8::ToUtf16MeasureResult emptyToUtf16Measure = Unicode::Utf8::measureToUtf16({});
    const Unicode::Types::Utf16::ToUtf8MeasureResult emptyToUtf8Measure = Unicode::Utf16::measureToUtf8({});
    static_cast<void>(context.expectEq("empty UTF-8 measurement outcome", MeasureOutcome::Measured, emptyToUtf16Measure.outcome));
    static_cast<void>(context.expectEq("empty UTF-8 measurement size", std::size_t{0}, emptyToUtf16Measure.requiredCodeUnits));
    static_cast<void>(context.expectEq("empty UTF-16 measurement outcome", MeasureOutcome::Measured, emptyToUtf8Measure.outcome));
    static_cast<void>(context.expectEq("empty UTF-16 measurement size", std::size_t{0}, emptyToUtf8Measure.requiredBytes));

    const std::string incompleteUtf8 = std::string("A") + bytes({0xE2, 0x82});
    const Unicode::Types::Utf8::ToUtf16MeasureResult incompleteUtf8Measure = Unicode::Utf8::measureToUtf16(incompleteUtf8);
    static_cast<void>(context.expectEq("incomplete UTF-8 measurement outcome", MeasureOutcome::Incomplete, incompleteUtf8Measure.outcome));
    static_cast<void>(context.expectEq("incomplete UTF-8 measurement source progress", std::size_t{1}, incompleteUtf8Measure.sourceBytesProcessed));
    static_cast<void>(context.expectEq("incomplete UTF-8 measurement destination progress", std::size_t{1}, incompleteUtf8Measure.requiredCodeUnits));

    const std::string invalidUtf8 = std::string("A") + bytes({0xE2, 0x28});
    const Unicode::Types::Utf8::ToUtf16MeasureResult invalidUtf8Measure = Unicode::Utf8::measureToUtf16(invalidUtf8);
    static_cast<void>(context.expectEq("invalid UTF-8 measurement outcome", MeasureOutcome::InvalidEncoding, invalidUtf8Measure.outcome));
    static_cast<void>(context.expectEq("invalid UTF-8 measurement source progress", std::size_t{1}, invalidUtf8Measure.sourceBytesProcessed));
    static_cast<void>(context.expectEq("invalid UTF-8 measurement destination progress", std::size_t{1}, invalidUtf8Measure.requiredCodeUnits));

    const std::array<char16_t, 2> incompleteUtf16{u'A', static_cast<char16_t>(0xD800)};
    const Unicode::Types::Utf16::ToUtf8MeasureResult incompleteUtf16Measure = Unicode::Utf16::measureToUtf8(incompleteUtf16);
    static_cast<void>(context.expectEq("incomplete UTF-16 measurement outcome", MeasureOutcome::Incomplete, incompleteUtf16Measure.outcome));
    static_cast<void>(
        context.expectEq("incomplete UTF-16 measurement source progress", std::size_t{1}, incompleteUtf16Measure.sourceCodeUnitsProcessed));
    static_cast<void>(context.expectEq("incomplete UTF-16 measurement destination progress", std::size_t{1}, incompleteUtf16Measure.requiredBytes));

    const std::array<char16_t, 3> invalidUtf16{u'A', static_cast<char16_t>(0xD800), u'B'};
    const Unicode::Types::Utf16::ToUtf8MeasureResult invalidUtf16Measure = Unicode::Utf16::measureToUtf8(invalidUtf16);
    static_cast<void>(context.expectEq("invalid UTF-16 measurement outcome", MeasureOutcome::InvalidEncoding, invalidUtf16Measure.outcome));
    static_cast<void>(context.expectEq("invalid UTF-16 measurement source progress", std::size_t{1}, invalidUtf16Measure.sourceCodeUnitsProcessed));
    static_cast<void>(context.expectEq("invalid UTF-16 measurement destination progress", std::size_t{1}, invalidUtf16Measure.requiredBytes));

    // Verify exact conversion and untouched destination tails.
    std::array<char16_t, 5> exactUtf16Destination{};
    const Unicode::Types::Utf8::ToUtf16Result exactToUtf16 = Unicode::Utf8::convertToUtf16(utf8, exactUtf16Destination);
    static_cast<void>(context.expectEq("exact UTF-8 to UTF-16 conversion outcome", ConversionOutcome::Converted, exactToUtf16.outcome));
    static_cast<void>(context.expectTrue("exact UTF-8 to UTF-16 payload", std::equal(utf16.begin(), utf16.end(), exactUtf16Destination.begin())));

    std::array<char, 10> exactUtf8Destination{};
    const Unicode::Types::Utf16::ToUtf8Result exactToUtf8 = Unicode::Utf16::convertToUtf8(utf16, exactUtf8Destination);
    static_cast<void>(context.expectEq("exact UTF-16 to UTF-8 conversion outcome", ConversionOutcome::Converted, exactToUtf8.outcome));
    static_cast<void>(context.expectEq(
        "exact UTF-16 to UTF-8 payload",
        std::string_view(utf8),
        std::string_view(exactUtf8Destination.data(), exactUtf8Destination.size())));

    std::array<char16_t, 7> utf16Destination;
    utf16Destination.fill(static_cast<char16_t>(0xCAFE));
    const Unicode::Types::Utf8::ToUtf16Result toUtf16 = Unicode::Utf8::convertToUtf16(utf8, utf16Destination);
    static_cast<void>(context.expectEq("UTF-8 to UTF-16 conversion outcome", ConversionOutcome::Converted, toUtf16.outcome));
    static_cast<void>(context.expectEq("UTF-8 to UTF-16 source progress", utf8.size(), toUtf16.sourceBytesConsumed));
    static_cast<void>(context.expectEq("UTF-8 to UTF-16 destination progress", utf16.size(), toUtf16.codeUnitsWritten));
    static_cast<void>(context.expectTrue("UTF-8 to UTF-16 exact payload", std::equal(utf16.begin(), utf16.end(), utf16Destination.begin())));
    static_cast<void>(context.expectEq("UTF-8 to UTF-16 leaves first tail sentinel", static_cast<char16_t>(0xCAFE), utf16Destination[5]));
    static_cast<void>(context.expectEq("UTF-8 to UTF-16 leaves second tail sentinel", static_cast<char16_t>(0xCAFE), utf16Destination[6]));

    std::array<char, 12> utf8Destination;
    utf8Destination.fill(static_cast<char>(0x5A));
    const Unicode::Types::Utf16::ToUtf8Result toUtf8 = Unicode::Utf16::convertToUtf8(utf16, utf8Destination);
    static_cast<void>(context.expectEq("UTF-16 to UTF-8 conversion outcome", ConversionOutcome::Converted, toUtf8.outcome));
    static_cast<void>(context.expectEq("UTF-16 to UTF-8 source progress", utf16.size(), toUtf8.sourceCodeUnitsConsumed));
    static_cast<void>(context.expectEq("UTF-16 to UTF-8 destination progress", utf8.size(), toUtf8.bytesWritten));
    static_cast<void>(
        context.expectEq("UTF-16 to UTF-8 exact payload", std::string_view(utf8), std::string_view(utf8Destination.data(), utf8.size())));
    static_cast<void>(context.expectEq("UTF-16 to UTF-8 leaves first tail sentinel", static_cast<char>(0x5A), utf8Destination[10]));
    static_cast<void>(context.expectEq("UTF-16 to UTF-8 leaves second tail sentinel", static_cast<char>(0x5A), utf8Destination[11]));

    // Stop before a scalar that cannot fit completely.
    const std::string scalarBoundaryUtf8 = std::string("A") + bytes({0xF0, 0x9F, 0x98, 0x80}) + "B";
    std::array<char16_t, 4> smallUtf16;
    smallUtf16.fill(static_cast<char16_t>(0xBEEF));
    const Unicode::Types::Utf8::ToUtf16Result smallToUtf16 =
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
    const Unicode::Types::Utf16::ToUtf8Result smallToUtf8 = Unicode::Utf16::convertToUtf8(scalarBoundaryUtf16, std::span<char>(smallUtf8).first(4));
    static_cast<void>(
        context.expectEq("UTF-16 conversion stops before partial UTF-8 scalar", ConversionOutcome::DestinationTooSmall, smallToUtf8.outcome));
    static_cast<void>(context.expectEq("UTF-16 conversion partial source progress", std::size_t{1}, smallToUtf8.sourceCodeUnitsConsumed));
    static_cast<void>(context.expectEq("UTF-16 conversion partial destination progress", std::size_t{1}, smallToUtf8.bytesWritten));
    static_cast<void>(context.expectEq("UTF-16 conversion preserves first unwritten byte", static_cast<char>(0x66), smallUtf8[1]));
    static_cast<void>(context.expectEq("UTF-16 conversion preserves destination tail", static_cast<char>(0x66), smallUtf8[4]));

    // Preserve the valid prefix when input is incomplete or malformed.
    std::array<char16_t, 4> incompleteUtf16Destination;
    incompleteUtf16Destination.fill(static_cast<char16_t>(0x3333));
    const Unicode::Types::Utf8::ToUtf16Result incompleteUtf8Conversion = Unicode::Utf8::convertToUtf16(incompleteUtf8, incompleteUtf16Destination);
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
    const Unicode::Types::Utf16::ToUtf8Result incompleteUtf16Conversion = Unicode::Utf16::convertToUtf8(incompleteUtf16, incompleteUtf8Destination);
    static_cast<void>(context.expectEq("incomplete UTF-16 conversion outcome", ConversionOutcome::Incomplete, incompleteUtf16Conversion.outcome));
    static_cast<void>(
        context.expectEq("incomplete UTF-16 conversion source progress", std::size_t{1}, incompleteUtf16Conversion.sourceCodeUnitsConsumed));
    static_cast<void>(context.expectEq("incomplete UTF-16 conversion destination progress", std::size_t{1}, incompleteUtf16Conversion.bytesWritten));
    static_cast<void>(
        context.expectEq("incomplete UTF-16 conversion preserves destination after prefix", static_cast<char>(0x33), incompleteUtf8Destination[1]));

    std::array<char16_t, 4> malformedUtf16Destination;
    malformedUtf16Destination.fill(static_cast<char16_t>(0x4444));
    const Unicode::Types::Utf8::ToUtf16Result invalidUtf8Conversion = Unicode::Utf8::convertToUtf16(invalidUtf8, malformedUtf16Destination);
    static_cast<void>(context.expectEq("invalid UTF-8 conversion outcome", ConversionOutcome::InvalidEncoding, invalidUtf8Conversion.outcome));
    static_cast<void>(context.expectEq("invalid UTF-8 conversion source progress", std::size_t{1}, invalidUtf8Conversion.sourceBytesConsumed));
    static_cast<void>(context.expectEq("invalid UTF-8 conversion destination progress", std::size_t{1}, invalidUtf8Conversion.codeUnitsWritten));
    static_cast<void>(
        context.expectEq("invalid UTF-8 conversion preserves destination after prefix", static_cast<char16_t>(0x4444), malformedUtf16Destination[1]));

    std::array<char, 8> malformedUtf8Destination;
    malformedUtf8Destination.fill(static_cast<char>(0x44));
    const Unicode::Types::Utf16::ToUtf8Result invalidUtf16Conversion = Unicode::Utf16::convertToUtf8(invalidUtf16, malformedUtf8Destination);
    static_cast<void>(context.expectEq("invalid UTF-16 conversion outcome", ConversionOutcome::InvalidEncoding, invalidUtf16Conversion.outcome));
    static_cast<void>(context.expectEq("invalid UTF-16 conversion source progress", std::size_t{1}, invalidUtf16Conversion.sourceCodeUnitsConsumed));
    static_cast<void>(context.expectEq("invalid UTF-16 conversion destination progress", std::size_t{1}, invalidUtf16Conversion.bytesWritten));
    static_cast<void>(
        context.expectEq("invalid UTF-16 conversion preserves destination after prefix", static_cast<char>(0x44), malformedUtf8Destination[1]));

    // Empty input succeeds without modifying destination storage.
    std::array<char16_t, 1> emptyUtf16Destination{static_cast<char16_t>(0x7777)};
    const Unicode::Types::Utf8::ToUtf16Result emptyToUtf16 = Unicode::Utf8::convertToUtf16({}, emptyUtf16Destination);
    static_cast<void>(context.expectEq("empty UTF-8 conversion outcome", ConversionOutcome::Converted, emptyToUtf16.outcome));
    static_cast<void>(context.expectEq("empty UTF-8 conversion writes nothing", std::size_t{0}, emptyToUtf16.codeUnitsWritten));
    static_cast<void>(context.expectEq("empty UTF-8 conversion preserves destination", static_cast<char16_t>(0x7777), emptyUtf16Destination[0]));

    std::array<char, 1> emptyUtf8Destination{static_cast<char>(0x77)};
    const Unicode::Types::Utf16::ToUtf8Result emptyToUtf8 = Unicode::Utf16::convertToUtf8({}, emptyUtf8Destination);
    static_cast<void>(context.expectEq("empty UTF-16 conversion outcome", ConversionOutcome::Converted, emptyToUtf8.outcome));
    static_cast<void>(context.expectEq("empty UTF-16 conversion writes nothing", std::size_t{0}, emptyToUtf8.bytesWritten));
    static_cast<void>(context.expectEq("empty UTF-16 conversion preserves destination", static_cast<char>(0x77), emptyUtf8Destination[0]));

    // Reject overlapping source and destination ranges.
    std::array<char16_t, 4> overlapStorage{
        static_cast<char16_t>(0x4141),
        static_cast<char16_t>(0x4242),
        static_cast<char16_t>(0x4343),
        static_cast<char16_t>(0x4444),
    };
    const std::string_view overlappingUtf8(reinterpret_cast<const char *>(overlapStorage.data()), 4);
    const Unicode::Types::Utf8::ToUtf16Result overlapToUtf16 = Unicode::Utf8::convertToUtf16(overlappingUtf8, overlapStorage);
    static_cast<void>(context.expectEq("UTF-8 to UTF-16 overlap outcome", ConversionOutcome::OverlappingRanges, overlapToUtf16.outcome));
    static_cast<void>(context.expectEq("UTF-8 to UTF-16 overlap consumes nothing", std::size_t{0}, overlapToUtf16.sourceBytesConsumed));
    static_cast<void>(context.expectEq("UTF-8 to UTF-16 overlap writes nothing", std::size_t{0}, overlapToUtf16.codeUnitsWritten));

    std::array<char16_t, 4> reverseOverlapStorage{u'A', u'B', u'C', u'D'};
    // This test deliberately aliases differently typed storage to validate reverse-overlap handling.
#if defined(__clang__)
#pragma clang unsafe_buffer_usage begin
#endif
    const std::span<char> overlappingUtf8Destination(
        reinterpret_cast<char *>(reverseOverlapStorage.data()),
        reverseOverlapStorage.size() * sizeof(char16_t));
#if defined(__clang__)
#pragma clang unsafe_buffer_usage end
#endif
    const Unicode::Types::Utf16::ToUtf8Result overlapToUtf8 = Unicode::Utf16::convertToUtf8(reverseOverlapStorage, overlappingUtf8Destination);
    static_cast<void>(context.expectEq("UTF-16 to UTF-8 overlap outcome", ConversionOutcome::OverlappingRanges, overlapToUtf8.outcome));
    static_cast<void>(context.expectEq("UTF-16 to UTF-8 overlap consumes nothing", std::size_t{0}, overlapToUtf8.sourceCodeUnitsConsumed));
    static_cast<void>(context.expectEq("UTF-16 to UTF-8 overlap writes nothing", std::size_t{0}, overlapToUtf8.bytesWritten));
}
