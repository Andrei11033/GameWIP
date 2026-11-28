/// @file scalar_round_trip_test.inl
/// @brief Exhaustive Unicode scalar round-trip test.

/// @brief Exhaustively verifies scalar round trips and every proper UTF-8 split point.
void testExhaustiveScalarRoundTrips(TestSupport::Context &context)
{
    std::size_t testedScalars = 0;

    // Round-trip every scalar through both encodings and conversion paths.
    for(std::uint32_t value = 0; value <= 0x10FFFFU; ++value)
    {
        const char32_t scalar = static_cast<char32_t>(value);

        if(!Unicode::isScalarValue(scalar))
        {
            continue;
        }

        const Unicode::Types::Utf8::EncodeResult utf8 = Unicode::Utf8::encodeScalar(scalar);

        if(utf8.outcome != EncodeOutcome::Encoded)
        {
            context.fail("exhaustive UTF-8 encode", std::format("U+{:06X} did not encode", value));
            return;
        }
        const Unicode::Types::Utf8::DecodeResult utf8Decoded = Unicode::Utf8::decodeScalar(encodedBytes(utf8));

        if(utf8Decoded.outcome != DecodeOutcome::Decoded || utf8Decoded.scalar != scalar || utf8Decoded.bytesConsumed != utf8.byteCount)
        {
            context.fail("exhaustive UTF-8 round trip", std::format("U+{:06X} did not decode back exactly", value));
            return;
        }

        for(std::size_t split = 1; split < static_cast<std::size_t>(utf8.byteCount); ++split)
        {
            const Unicode::Types::Utf8::DecodeResult prefix = Unicode::Utf8::decodeScalar(std::string_view(utf8.bytes.data(), split));

            if(prefix.outcome != DecodeOutcome::Incomplete || prefix.scalar != U'\0' || prefix.bytesConsumed != 0)
            {
                context.fail(
                    "exhaustive UTF-8 split point",
                    std::format("U+{:06X} split at byte {} did not report deterministic incomplete input", value, split));
                return;
            }
        }

        const Unicode::Types::Utf16::EncodeResult utf16 = Unicode::Utf16::encodeScalar(scalar);

        if(utf16.outcome != EncodeOutcome::Encoded)
        {
            context.fail("exhaustive UTF-16 encode", std::format("U+{:06X} did not encode", value));
            return;
        }
        const Unicode::Types::Utf16::DecodeResult utf16Decoded = Unicode::Utf16::decodeScalar(encodedCodeUnits(utf16));

        if(utf16Decoded.outcome != DecodeOutcome::Decoded || utf16Decoded.scalar != scalar || utf16Decoded.codeUnitsConsumed != utf16.codeUnitCount)
        {
            context.fail("exhaustive UTF-16 round trip", std::format("U+{:06X} did not decode back exactly", value));
            return;
        }
        std::array<char16_t, Unicode::Utf16::kMaximumScalarCodeUnits> convertedUtf16{};
        const Unicode::Types::Utf8::ToUtf16Result toUtf16 = Unicode::Utf8::convertToUtf16(encodedBytes(utf8), convertedUtf16);

        if(toUtf16.outcome != ConversionOutcome::Converted || toUtf16.sourceBytesConsumed != utf8.byteCount ||
           toUtf16.codeUnitsWritten != utf16.codeUnitCount ||
           !std::ranges::equal(std::span{utf16.codeUnits}.first(utf16.codeUnitCount), std::span{convertedUtf16}.first(utf16.codeUnitCount)))
        {
            context.fail("exhaustive UTF-8 to UTF-16 conversion", std::format("U+{:06X} did not convert exactly", value));
            return;
        }

        std::array<char, Unicode::Utf8::kMaximumScalarBytes> convertedUtf8{};
        const Unicode::Types::Utf16::ToUtf8Result toUtf8 = Unicode::Utf16::convertToUtf8(encodedCodeUnits(utf16), convertedUtf8);

        if(toUtf8.outcome != ConversionOutcome::Converted || toUtf8.sourceCodeUnitsConsumed != utf16.codeUnitCount ||
           toUtf8.bytesWritten != utf8.byteCount ||
           !std::ranges::equal(std::span{utf8.bytes}.first(utf8.byteCount), std::span{convertedUtf8}.first(utf8.byteCount)))
        {
            context.fail("exhaustive UTF-16 to UTF-8 conversion", std::format("U+{:06X} did not convert exactly", value));
            return;
        }

        ++testedScalars;
    }

    // Confirm the loop covered every Unicode scalar value.
    static_cast<void>(context.expectEq("exhaustive scalar count", std::size_t{1112064}, testedScalars));
    context.pass("every Unicode scalar round-trips through UTF-8, UTF-16, and both conversion directions");
}
