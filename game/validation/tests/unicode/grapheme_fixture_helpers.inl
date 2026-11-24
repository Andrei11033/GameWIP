/// @file grapheme_fixture_helpers.inl
/// @brief Grapheme fixture construction and traversal checks.

/// ------------------------------------------------------------
/// Grapheme fixture checks
/// ------------------------------------------------------------

/// @brief UTF-8 text with scalar offsets and expected grapheme boundaries.
struct GraphemeFixture
{
    std::string text;                          ///< Encoded UTF-8 fixture text.
    std::vector<std::size_t> codePointOffsets; ///< Byte offsets after each scalar.
    std::vector<std::size_t> boundaries;       ///< Byte offsets between grapheme clusters.
};

/// @brief Builds UTF-8 text and expected byte boundaries from explicit grapheme clusters.
GraphemeFixture graphemeFixture(std::initializer_list<std::initializer_list<char32_t>> clusters)
{
    GraphemeFixture fixture;
    fixture.codePointOffsets.push_back(0);
    fixture.boundaries.push_back(0);

    for(const auto &cluster : clusters)
    {
        for(const char32_t scalar : cluster)
        {
            const Unicode::Types::Utf8::EncodeResult encoded = Unicode::Utf8::encodeScalar(scalar);

            if(encoded.outcome != EncodeOutcome::Encoded)
            {
                throw std::runtime_error(std::format("grapheme fixture U+{:04X} did not encode", static_cast<std::uint32_t>(scalar)));
            }
            fixture.text.append(encodedBytes(encoded));
            fixture.codePointOffsets.push_back(fixture.text.size());
        }
        fixture.boundaries.push_back(fixture.text.size());
    }

    return fixture;
}

/// @brief Returns the expected result of moving forward from one code-point-aligned offset.
Unicode::Types::Utf8::BoundaryResult expectedNextGrapheme(const GraphemeFixture &fixture, std::size_t offset)
{
    if(offset == fixture.text.size())
    {
        return {.byteOffset = offset, .outcome = BoundaryOutcome::AtEnd};
    }
    const auto boundary = std::upper_bound(fixture.boundaries.begin(), fixture.boundaries.end(), offset);
    return {.byteOffset = *boundary, .outcome = BoundaryOutcome::Found};
}

/// @brief Returns the expected result of moving backward from one code-point-aligned offset.
Unicode::Types::Utf8::BoundaryResult expectedPreviousGrapheme(const GraphemeFixture &fixture, std::size_t offset)
{
    if(offset == 0)
    {
        return {.byteOffset = 0, .outcome = BoundaryOutcome::AtBeginning};
    }
    const auto boundary = std::lower_bound(fixture.boundaries.begin(), fixture.boundaries.end(), offset);
    return {.byteOffset = *(boundary - 1), .outcome = BoundaryOutcome::Found};
}

/// @brief Returns a diagnostic when one fixture disagrees with stateless or indexed traversal.
std::optional<std::string> graphemeFailure(const GraphemeFixture &fixture)
{
    // Compare stateless traversal at every code-point offset.
    for(const std::size_t offset : fixture.codePointOffsets)
    {
        const Unicode::Types::Utf8::BoundaryResult expectedNext = expectedNextGrapheme(fixture, offset);
        const Unicode::Types::Utf8::BoundaryResult actualNext = Unicode::Utf8::nextGraphemeBoundary(fixture.text, offset);

        if(actualNext.outcome != expectedNext.outcome || actualNext.byteOffset != expectedNext.byteOffset)
        {
            return std::format(
                "next offset {} expected outcome={} offset={} but got outcome={} offset={}",
                offset,
                static_cast<unsigned int>(expectedNext.outcome),
                expectedNext.byteOffset,
                static_cast<unsigned int>(actualNext.outcome),
                actualNext.byteOffset);
        }

        const Unicode::Types::Utf8::BoundaryResult expectedPrevious = expectedPreviousGrapheme(fixture, offset);
        const Unicode::Types::Utf8::BoundaryResult actualPrevious = Unicode::Utf8::previousGraphemeBoundary(fixture.text, offset);

        if(actualPrevious.outcome != expectedPrevious.outcome || actualPrevious.byteOffset != expectedPrevious.byteOffset)
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

    // Build the bounded index before checking cursor traversal.
    std::vector<std::size_t> boundaryStorage(fixture.codePointOffsets.size());
    Unicode::Utf8::GraphemeCursor cursor;
    const Unicode::Types::Utf8::GraphemeIndexResult indexed = cursor.reset(fixture.text, boundaryStorage);

    if(indexed.outcome != Unicode::Types::Utf8::GraphemeIndexOutcome::Indexed || indexed.requiredBoundaryCount != fixture.boundaries.size() ||
       cursor.boundaryCount() != fixture.boundaries.size())
    {
        return std::format(
            "cursor index expected boundaries={} but got outcome={} required={} retained={}",
            fixture.boundaries.size(),
            static_cast<unsigned int>(indexed.outcome),
            indexed.requiredBoundaryCount,
            cursor.boundaryCount());
    }

    // Check indexed traversal in both directions.
    for(std::size_t index = 1; index < fixture.boundaries.size(); ++index)
    {
        const Unicode::Types::Utf8::BoundaryResult actual = cursor.next();

        if(actual.outcome != BoundaryOutcome::Found || actual.byteOffset != fixture.boundaries[index])
        {
            return std::format(
                "cursor next index {} expected offset={} but got outcome={} offset={}",
                index,
                fixture.boundaries[index],
                static_cast<unsigned int>(actual.outcome),
                actual.byteOffset);
        }
    }

    const Unicode::Types::Utf8::BoundaryResult atEnd = cursor.next();

    if(atEnd.outcome != BoundaryOutcome::AtEnd || atEnd.byteOffset != fixture.text.size())
    {
        return std::format(
            "cursor end expected offset={} but got outcome={} offset={}",
            fixture.text.size(),
            static_cast<unsigned int>(atEnd.outcome),
            atEnd.byteOffset);
    }

    for(std::size_t index = fixture.boundaries.size() - 1; index > 0; --index)
    {
        const Unicode::Types::Utf8::BoundaryResult actual = cursor.previous();

        if(actual.outcome != BoundaryOutcome::Found || actual.byteOffset != fixture.boundaries[index - 1])
        {
            return std::format(
                "cursor previous index {} expected offset={} but got outcome={} offset={}",
                index,
                fixture.boundaries[index - 1],
                static_cast<unsigned int>(actual.outcome),
                actual.byteOffset);
        }
    }

    const Unicode::Types::Utf8::BoundaryResult atBeginning = cursor.previous();

    if(atBeginning.outcome != BoundaryOutcome::AtBeginning || atBeginning.byteOffset != 0)
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

    if(failure.has_value())
    {
        context.fail(name, *failure);
    }
    else
    {
        context.pass(name);
    }
}
