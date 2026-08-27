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
#include <memory>
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
    // ------------------------------------------------------------
    // Test data helpers
    // ------------------------------------------------------------

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
        return std::span{result.codeUnits}.first(result.codeUnitCount);
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
            const std::filesystem::path path = searchRoot / "build" / "gamewip" / "cache" / "unicode" / relativePath;
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
            const char *end = std::to_address(scalarText.cend());
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

    // Focused suite declarations keep cross-suite calls independent of fragment include order.
    // ------------------------------------------------------------
    // Test suites
    // ------------------------------------------------------------

    void testVersionAndScalarPredicates(TestSupport::Context &context);
    void testUtf8CodecAndValidation(TestSupport::Context &context);
    void testUtf16CodecAndValidation(TestSupport::Context &context);
    void testConversions(TestSupport::Context &context);
    void testExhaustiveScalarRoundTrips(TestSupport::Context &context);
    void testCodePointBoundaries(TestSupport::Context &context);
    void testTargetedGraphemeBoundaries(TestSupport::Context &context);
    void testGeneratedPropertyData(TestSupport::Context &context);
    void testOfficialGraphemeConformance(TestSupport::Context &context);

#include "validation/tests/unicode/grapheme_test.inl"
#include "validation/tests/unicode/utf16_test.inl"
#include "validation/tests/unicode/utf8_test.inl"
} // namespace

namespace GameWIP::Test
{
    // ------------------------------------------------------------
    // Test runner
    // ------------------------------------------------------------

    int runUnicodeTests(int, char **, const UnicodeTestOptions &options)
    {
        TestSupport::Types::Reporting::Options reportOptions;
        reportOptions.writeConsole = true;
        reportOptions.consoleVerbosity =
            options.verboseConsole ? TestSupport::Types::Reporting::ConsoleVerbosity::Full : TestSupport::Types::Reporting::ConsoleVerbosity::Minimal;
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

        const TestSupport::Types::Reporting::Summary result = runner.result();
        runner.summary(std::format("Unicode library self-tests passed={} failed={} skipped={}", result.passed, result.failed, result.skipped));
        return runner.exitCode();
    }
} // namespace GameWIP::Test
