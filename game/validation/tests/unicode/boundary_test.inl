/// @file boundary_test.inl
/// @brief Unicode code-point boundary and generated-property tests.

/// ------------------------------------------------------------
/// Boundaries and generated properties
/// ------------------------------------------------------------

/// @brief Verifies code-point traversal endpoints, alignment policy, malformed input, and multibyte offsets.
void testCodePointBoundaries(TestSupport::Context &context)
{
    const std::string text = bytes({0x41, 0xC2, 0xA2, 0xE2, 0x82, 0xAC, 0xF0, 0x9F, 0x98, 0x80});
    const std::array<std::size_t, 5> offsets{0, 1, 3, 6, 10};

    // Handle empty input and valid endpoints.
    const Unicode::Types::Utf8::BoundaryResult emptyNext = Unicode::Utf8::nextCodePointBoundary({}, 0);
    const Unicode::Types::Utf8::BoundaryResult emptyPrevious = Unicode::Utf8::previousCodePointBoundary({}, 0);
    static_cast<void>(context.expectEq("empty next code-point outcome", BoundaryOutcome::AtEnd, emptyNext.outcome));
    static_cast<void>(context.expectEq("empty previous code-point outcome", BoundaryOutcome::AtBeginning, emptyPrevious.outcome));

    for(std::size_t index = 0; index + 1 < offsets.size(); ++index)
    {
        const std::string name = std::format("next code-point boundary at byte {}", offsets[index]);
        const Unicode::Types::Utf8::BoundaryResult next = Unicode::Utf8::nextCodePointBoundary(text, offsets[index]);
        static_cast<void>(context.expectEq(name + " outcome", BoundaryOutcome::Found, next.outcome));
        static_cast<void>(context.expectEq(name + " offset", offsets[index + 1], next.byteOffset));
    }
    const Unicode::Types::Utf8::BoundaryResult nextEnd = Unicode::Utf8::nextCodePointBoundary(text, text.size());
    static_cast<void>(context.expectEq("next code-point boundary at end outcome", BoundaryOutcome::AtEnd, nextEnd.outcome));
    static_cast<void>(context.expectEq("next code-point boundary at end offset", text.size(), nextEnd.byteOffset));

    const Unicode::Types::Utf8::BoundaryResult previousBeginning = Unicode::Utf8::previousCodePointBoundary(text, 0);
    static_cast<void>(context.expectEq("previous code-point boundary at beginning outcome", BoundaryOutcome::AtBeginning, previousBeginning.outcome));
    static_cast<void>(context.expectEq("previous code-point boundary at beginning offset", std::size_t{0}, previousBeginning.byteOffset));

    // Traverse each valid boundary in both directions.
    for(std::size_t index = 1; index < offsets.size(); ++index)
    {
        const std::string name = std::format("previous code-point boundary at byte {}", offsets[index]);
        const Unicode::Types::Utf8::BoundaryResult previous = Unicode::Utf8::previousCodePointBoundary(text, offsets[index]);
        static_cast<void>(context.expectEq(name + " outcome", BoundaryOutcome::Found, previous.outcome));
        static_cast<void>(context.expectEq(name + " offset", offsets[index - 1], previous.byteOffset));
    }

    // Reject offsets inside scalars or past the end.
    for(const std::size_t misaligned : {std::size_t{2}, std::size_t{4}, std::size_t{5}, std::size_t{7}, std::size_t{8}, std::size_t{9}})
    {
        const std::string name = std::format("misaligned code-point boundary at byte {}", misaligned);
        const Unicode::Types::Utf8::BoundaryResult next = Unicode::Utf8::nextCodePointBoundary(text, misaligned);
        const Unicode::Types::Utf8::BoundaryResult previous = Unicode::Utf8::previousCodePointBoundary(text, misaligned);
        static_cast<void>(context.expectEq(name + " next outcome", BoundaryOutcome::InvalidOffset, next.outcome));
        static_cast<void>(context.expectEq(name + " next preserves offset", misaligned, next.byteOffset));
        static_cast<void>(context.expectEq(name + " previous outcome", BoundaryOutcome::InvalidOffset, previous.outcome));
        static_cast<void>(context.expectEq(name + " previous preserves offset", misaligned, previous.byteOffset));
    }

    const Unicode::Types::Utf8::BoundaryResult beyondNext = Unicode::Utf8::nextCodePointBoundary(text, text.size() + 1);
    const Unicode::Types::Utf8::BoundaryResult beyondPrevious = Unicode::Utf8::previousCodePointBoundary(text, text.size() + 1);
    static_cast<void>(context.expectEq("beyond-end next code-point outcome", BoundaryOutcome::InvalidOffset, beyondNext.outcome));
    static_cast<void>(context.expectEq("beyond-end previous code-point outcome", BoundaryOutcome::InvalidOffset, beyondPrevious.outcome));

    // Reject incomplete and malformed UTF-8.
    const std::string incomplete = bytes({0xC2});
    const Unicode::Types::Utf8::BoundaryResult incompleteNext = Unicode::Utf8::nextCodePointBoundary(incomplete, 0);
    const Unicode::Types::Utf8::BoundaryResult incompletePrevious = Unicode::Utf8::previousCodePointBoundary(incomplete, 1);
    static_cast<void>(context.expectEq("incomplete next code-point outcome", BoundaryOutcome::InvalidEncoding, incompleteNext.outcome));
    static_cast<void>(context.expectEq("incomplete previous code-point outcome", BoundaryOutcome::InvalidEncoding, incompletePrevious.outcome));

    const std::string malformed = std::string("A") + bytes({0xFF});
    const Unicode::Types::Utf8::BoundaryResult malformedNext = Unicode::Utf8::nextCodePointBoundary(malformed, 1);
    const Unicode::Types::Utf8::BoundaryResult malformedPrevious = Unicode::Utf8::previousCodePointBoundary(malformed, 2);
    static_cast<void>(context.expectEq("malformed next code-point outcome", BoundaryOutcome::InvalidEncoding, malformedNext.outcome));
    static_cast<void>(context.expectEq("malformed previous code-point outcome", BoundaryOutcome::InvalidEncoding, malformedPrevious.outcome));
}

/// @brief Verifies generated-table invariants and representative algorithmic/property classifications.
void testGeneratedPropertyData(TestSupport::Context &context)
{
    namespace Generated = Unicode::Internal::Generated;
    using Unicode::Internal::GraphemeBreakClass;
    using Unicode::Internal::IndicConjunctBreakClass;

    // Check generated version and table shape.
    static_cast<void>(context.expectEq("generated major version", std::uint8_t{18}, Generated::kUnicodeVersionMajor));
    static_cast<void>(context.expectEq("generated minor version", std::uint8_t{0}, Generated::kUnicodeVersionMinor));
    static_cast<void>(context.expectEq("generated patch version", std::uint8_t{0}, Generated::kUnicodeVersionPatch));
    static_cast<void>(context.expectEq("generated block size", std::size_t{1} << Generated::kBlockShift, Generated::kBlockSize));
    static_cast<void>(context.expectEq("generated block mask", Generated::kBlockSize - 1, Generated::kBlockMask));
    static_cast<void>(context.expectTrue(
        "generated block size is within the performance-safe generator search range",
        Generated::kBlockSize >= 32 && Generated::kBlockSize <= 512));
    static_cast<void>(context.expectEq(
        "generated index coverage",
        static_cast<std::size_t>(Generated::kHighStart) >> Generated::kBlockShift,
        Generated::kBlockIndexes.size()));
    static_cast<void>(
        context.expectEq("generated property block alignment", std::size_t{0}, Generated::kPropertyBlocks.size() % Generated::kBlockSize));

    const std::size_t blockCount = Generated::kPropertyBlocks.size() / Generated::kBlockSize;
    bool indexesValid = true;

    for(const Generated::BlockIndex blockIndex : Generated::kBlockIndexes)
    {
        if(static_cast<std::size_t>(blockIndex) >= blockCount)
        {
            indexesValid = false;
            break;
        }
    }
    static_cast<void>(context.expectTrue("every generated block index is in range", indexesValid));

    // Validate each packed property entry.
    bool packedValuesValid = true;

    for(const std::uint8_t packed : Generated::kPropertyBlocks)
    {
        if((packed & 0x80U) != 0 || (packed & 0x0FU) > 13U)
        {
            packedValuesValid = false;
            break;
        }
    }
    static_cast<void>(context.expectTrue("every generated packed property value uses the defined bit layout", packedValuesValid));

    // Check representative algorithmic and generated properties.
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
