/// @file grapheme_conformance_helpers.inl
/// @brief Grapheme conformance data lookup and parsing.

/// ------------------------------------------------------------
/// Conformance data helpers
/// ------------------------------------------------------------

/// @brief Resolves the official Unicode 17 grapheme conformance file from explicit or GameWIP cache paths.
std::optional<std::filesystem::path> findGraphemeBreakTestFile()
{
    constexpr std::string_view relativePath = "17.0.0/ucd/auxiliary/GraphemeBreakTest.txt";

    // Check an explicit fixture override first.
    if(const char *explicitPath = std::getenv("GAMEWIP_UNICODE_GRAPHEME_BREAK_TEST"); explicitPath != nullptr && *explicitPath != '\0')
    {
        const std::filesystem::path path(explicitPath);

        if(std::filesystem::is_regular_file(path))
        {
            return path;
        }
    }

    // Check the configured Unicode data cache.
    if(const char *cacheRoot = std::getenv("GAMEWIP_UNICODE_DATA_ROOT"); cacheRoot != nullptr && *cacheRoot != '\0')
    {
        const std::filesystem::path path = std::filesystem::path(cacheRoot) / relativePath;

        if(std::filesystem::is_regular_file(path))
        {
            return path;
        }
    }

    // Search parent directories for the project cache.
    std::error_code currentPathError;
    std::filesystem::path searchRoot = std::filesystem::current_path(currentPathError);

    if(currentPathError)
    {
        return std::nullopt;
    }

    for(std::size_t depth = 0; depth < 8; ++depth)
    {
        const std::filesystem::path path = searchRoot / "build" / "gamewip" / "cache" / "unicode" / relativePath;

        if(std::filesystem::is_regular_file(path))
        {
            return path;
        }

        if(!searchRoot.has_parent_path() || searchRoot.parent_path() == searchRoot)
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

    if(!(tokens >> marker))
    {
        return std::nullopt;
    }

    if(marker != breakMarker)
    {
        error = "case does not begin with a break marker";
        return std::nullopt;
    }

    GraphemeFixture fixture;
    fixture.codePointOffsets.push_back(0);
    fixture.boundaries.push_back(0);

    // Parse scalar/boundary pairs into the expected fixture.
    std::string scalarText;

    while(tokens >> scalarText)
    {
        std::uint32_t scalarValue = 0;
        const char *begin = scalarText.data();
        const char *end = std::to_address(scalarText.cend());
        const auto parsed = std::from_chars(begin, end, scalarValue, 16);

        if(parsed.ec != std::errc{} || parsed.ptr != end || scalarValue > 0x10FFFFU)
        {
            error = std::format("invalid scalar token '{}'", scalarText);
            return std::nullopt;
        }
        const char32_t scalar = static_cast<char32_t>(scalarValue);
        const Unicode::Types::Utf8::EncodeResult encoded = Unicode::Utf8::encodeScalar(scalar);

        if(encoded.outcome != EncodeOutcome::Encoded)
        {
            error = std::format("non-scalar token '{}'", scalarText);
            return std::nullopt;
        }

        if(encoded.byteCount == 0 || encoded.byteCount > encoded.bytes.size())
        {
            error = std::format("token '{}' encoded with invalid byte count {}", scalarText, encoded.byteCount);
            return std::nullopt;
        }
        fixture.text.append(encodedBytes(encoded));
        fixture.codePointOffsets.push_back(fixture.text.size());

        if(!(tokens >> marker))
        {
            error = "case ends without a boundary marker";
            return std::nullopt;
        }

        if(marker == breakMarker)
        {
            fixture.boundaries.push_back(fixture.text.size());
        }
        else if(marker != noBreakMarker)
        {
            error = std::format("unknown boundary marker '{}'", marker);
            return std::nullopt;
        }
    }

    if(fixture.codePointOffsets.size() == 1 || fixture.boundaries.empty() || fixture.boundaries.back() != fixture.text.size())
    {
        error = "case is empty or does not end with a break marker";
        return std::nullopt;
    }
    return fixture;
}
