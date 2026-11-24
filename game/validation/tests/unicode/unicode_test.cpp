/// @file unicode_test.cpp
/// @brief Unicode correctness, exhaustive scalar, generated-data, and grapheme-conformance tests.

#include "validation/tests/unicode/unicode_test.h"

#include "test_support/test_support.h"
#include "validation/tests/internal/helpers/runner_helpers.h"
#include "unicode/internal/generated/unicode_properties.h"
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
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace
{
    namespace Unicode = GameWIP::Unicode;
    namespace TestSupport = GameWIP::TestSupport;

    using Unicode::Types::ConversionOutcome;
    using Unicode::Types::DecodeOutcome;
    using Unicode::Types::EncodeOutcome;
    using Unicode::Types::MeasureOutcome;
    using Unicode::Types::ValidationOutcome;
    using Unicode::Types::Utf8::BoundaryOutcome;

    // Scalar and encoding boundaries
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

    // UTF-8 operation contracts
    static_assert(noexcept(Unicode::Utf8::decodeScalar(std::declval<std::string_view>())));
    static_assert(noexcept(Unicode::Utf8::encodeScalar(U'A')));
    static_assert(noexcept(Unicode::Utf8::validate(std::declval<std::string_view>())));
    static_assert(noexcept(Unicode::Utf8::measureToUtf16(std::declval<std::string_view>())));
    static_assert(noexcept(Unicode::Utf8::convertToUtf16(std::declval<std::string_view>(), std::declval<std::span<char16_t>>())));
    static_assert(noexcept(Unicode::Utf8::nextCodePointBoundary(std::declval<std::string_view>(), 0)));
    static_assert(noexcept(Unicode::Utf8::previousCodePointBoundary(std::declval<std::string_view>(), 0)));
    static_assert(noexcept(Unicode::Utf8::nextGraphemeBoundary(std::declval<std::string_view>(), 0)));
    static_assert(noexcept(Unicode::Utf8::previousGraphemeBoundary(std::declval<std::string_view>(), 0)));

    // UTF-16 operation contracts
    static_assert(noexcept(Unicode::Utf16::decodeScalar(std::declval<std::span<const char16_t>>())));
    static_assert(noexcept(Unicode::Utf16::encodeScalar(U'A')));
    static_assert(noexcept(Unicode::Utf16::validate(std::declval<std::span<const char16_t>>())));
    static_assert(noexcept(Unicode::Utf16::measureToUtf8(std::declval<std::span<const char16_t>>())));
    static_assert(noexcept(Unicode::Utf16::convertToUtf8(std::declval<std::span<const char16_t>>(), std::declval<std::span<char>>())));

    /// ------------------------------------------------------------
    /// Scalar and codec helpers
    /// ------------------------------------------------------------

#include "validation/tests/unicode/unicode_test_helpers.inl"

    /// ------------------------------------------------------------
    /// Grapheme helpers
    /// ------------------------------------------------------------

#include "validation/tests/unicode/grapheme_fixture_helpers.inl"
#include "validation/tests/unicode/grapheme_conformance_helpers.inl"

    /// ------------------------------------------------------------
    /// Test suites
    /// ------------------------------------------------------------

    // Focused suite declarations keep cross-suite calls independent of fragment include order.
    void testVersionAndScalarPredicates(TestSupport::Context &context);
    void testUtf8CodecAndValidation(TestSupport::Context &context);
    void testUtf16CodecAndValidation(TestSupport::Context &context);
    void testConversions(TestSupport::Context &context);
    void testExhaustiveScalarRoundTrips(TestSupport::Context &context);
    void testCodePointBoundaries(TestSupport::Context &context);
    void testTargetedGraphemeBoundaries(TestSupport::Context &context);
    void testGeneratedPropertyData(TestSupport::Context &context);
    void testOfficialGraphemeConformance(TestSupport::Context &context);

#include "validation/tests/unicode/utf8_test.inl"
#include "validation/tests/unicode/utf16_test.inl"
#include "validation/tests/unicode/conversion_test.inl"
#include "validation/tests/unicode/scalar_round_trip_test.inl"
#include "validation/tests/unicode/boundary_test.inl"
#include "validation/tests/unicode/grapheme_test.inl"
} // namespace

namespace GameWIP::Test
{
    /// ------------------------------------------------------------
    /// Test runner
    /// ------------------------------------------------------------

    int runUnicodeTests(int, char **, const UnicodeTestOptions &options)
    {
        const TestSupport::Types::Reporting::Options reportOptions = GameWIP::Test::Internal::RunnerHelpers::makeReportingOptions(options);

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
        return GameWIP::Test::Internal::RunnerHelpers::finalizeRunner(runner);
    }
} // namespace GameWIP::Test
