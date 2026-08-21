/// @file grapheme_test.inl
/// @brief Focused unicode grapheme correctness suites.

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
    static_cast<void>(context.expectEq("grapheme cursor required boundary count", cursorFixture.boundaries.size(), tooSmall.requiredBoundaryCount));
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

/// @brief Executes the official Unicode 17 GraphemeBreakTest cases when versioned source data is available.
void testOfficialGraphemeConformance(TestSupport::Context &context)
{
    const std::optional<std::filesystem::path> conformancePath = findGraphemeBreakTestFile();
    if (!conformancePath.has_value())
    {
        constexpr std::string_view reason = "Unicode 17.0.0 GraphemeBreakTest.txt is not cached. Run '.\\gamewip.bat unicode -UnicodeAction verify' "
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
