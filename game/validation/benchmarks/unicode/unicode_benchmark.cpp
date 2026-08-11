/// @file unicode_benchmark.cpp
/// @brief Diagnostic hot-path benchmarks for Unicode encoding and boundary primitives.

#include "unicode/unicode.h"

#include <benchmark/benchmark.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    namespace Unicode = GameWIP::Unicode;

    constexpr std::string_view kAsciiText = "GameWIP Unicode benchmark payload: ASCII validation and traversal stay on the common fast path.";

    constexpr std::string_view kMixedText = "ASCII "
                                            "\xE2\x82\xAC "
                                            "\xF0\x9F\x98\x80 "
                                            "a\xCC\x88 "
                                            "\xF0\x9F\x87\xBA\xF0\x9F\x87\xB8";

    constexpr std::string_view kComplexGraphemeText = "A"
                                                      "a\xCC\x88"
                                                      "\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x92\xBB"
                                                      "\xF0\x9F\x87\xBA\xF0\x9F\x87\xB8";

    constexpr std::string_view kTraversalBlock = "a\xCC\x88"
                                                 "\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x92\xBB"
                                                 "\xE0\xA4\x95\xE0\xA5\x8D\xE0\xA4\x95"
                                                 "\xF0\x9F\x87\xBA\xF0\x9F\x87\xB8"
                                                 "X";

    constexpr std::size_t kLocalNextOffsetInBlock = 10;
    constexpr std::size_t kLocalPreviousOffsetInBlock = 14;

    /// @brief Returns a long mixed fixture with known grapheme boundaries at every block edge.
    [[nodiscard]] const std::string &longComplexGraphemeText()
    {
        static const std::string text = []
        {
            std::string value;
            value.reserve(kTraversalBlock.size() * 512);
            for (std::size_t index = 0; index < 512; ++index)
            {
                value.append(kTraversalBlock);
            }
            return value;
        }();
        return text;
    }

    constexpr std::array<char32_t, 8> kScalars{
        U'A',
        static_cast<char32_t>(0x7F),
        static_cast<char32_t>(0x80),
        static_cast<char32_t>(0x7FF),
        static_cast<char32_t>(0x800),
        static_cast<char32_t>(0x20AC),
        static_cast<char32_t>(0x1F600),
        static_cast<char32_t>(0x10FFFF),
    };

    /// @brief Measures repeated strict scalar decoding across one complete UTF-8 fixture.
    void benchmarkDecode(benchmark::State &state, std::string_view text)
    {
        for (auto iteration : state)
        {
            static_cast<void>(iteration);
            std::size_t offset = 0;
            while (offset < text.size())
            {
                Unicode::Types::Utf8DecodeResult decoded = Unicode::Utf8::decodeScalar(text.substr(offset));
                if (decoded.outcome != Unicode::Types::DecodeOutcome::Decoded)
                {
                    state.SkipWithError("Unicode decode benchmark fixture is invalid.");
                    return;
                }

                benchmark::DoNotOptimize(decoded.scalar);
                offset += decoded.bytesConsumed;
            }
        }

        state.SetBytesProcessed(state.iterations() * static_cast<std::int64_t>(text.size()));
    }

    /// @brief Measures repeated whole-range strict UTF-8 validation.
    void benchmarkValidation(benchmark::State &state, std::string_view text)
    {
        for (auto iteration : state)
        {
            static_cast<void>(iteration);
            Unicode::Types::Utf8ValidationResult result = Unicode::Utf8::validate(text);
            if (result.outcome != Unicode::Types::ValidationOutcome::Valid)
            {
                state.SkipWithError("Unicode validation benchmark fixture is invalid.");
                return;
            }

            benchmark::DoNotOptimize(result.validPrefixBytes);
        }

        state.SetBytesProcessed(state.iterations() * static_cast<std::int64_t>(text.size()));
    }

    /// @brief Measures sequential forward UTF-8 code-point traversal.
    void benchmarkForwardCodePoints(benchmark::State &state, std::string_view text)
    {
        for (auto iteration : state)
        {
            static_cast<void>(iteration);
            std::size_t offset = 0;
            while (offset < text.size())
            {
                Unicode::Types::Utf8BoundaryResult next = Unicode::Utf8::nextCodePointBoundary(text, offset);
                if (next.outcome != Unicode::Types::BoundaryOutcome::Found)
                {
                    state.SkipWithError("Unicode forward code-point traversal failed.");
                    return;
                }

                offset = next.byteOffset;
                benchmark::DoNotOptimize(next.byteOffset);
            }
        }

        state.SetBytesProcessed(state.iterations() * static_cast<std::int64_t>(text.size()));
    }

    /// @brief Measures sequential backward UTF-8 code-point traversal.
    void benchmarkBackwardCodePoints(benchmark::State &state, std::string_view text)
    {
        for (auto iteration : state)
        {
            static_cast<void>(iteration);
            std::size_t offset = text.size();
            while (offset > 0)
            {
                Unicode::Types::Utf8BoundaryResult previous = Unicode::Utf8::previousCodePointBoundary(text, offset);
                if (previous.outcome != Unicode::Types::BoundaryOutcome::Found)
                {
                    state.SkipWithError("Unicode backward code-point traversal failed.");
                    return;
                }

                offset = previous.byteOffset;
                benchmark::DoNotOptimize(previous.byteOffset);
            }
        }

        state.SetBytesProcessed(state.iterations() * static_cast<std::int64_t>(text.size()));
    }

    /// @brief Measures sequential forward extended grapheme-cluster traversal.
    void benchmarkForwardGraphemes(benchmark::State &state, std::string_view text)
    {
        for (auto iteration : state)
        {
            static_cast<void>(iteration);
            std::size_t offset = 0;
            while (offset < text.size())
            {
                Unicode::Types::Utf8BoundaryResult next = Unicode::Utf8::nextGraphemeBoundary(text, offset);
                if (next.outcome != Unicode::Types::BoundaryOutcome::Found)
                {
                    state.SkipWithError("Unicode forward grapheme traversal failed.");
                    return;
                }

                offset = next.byteOffset;
                benchmark::DoNotOptimize(next.byteOffset);
            }
        }

        state.SetBytesProcessed(state.iterations() * static_cast<std::int64_t>(text.size()));
    }

    /// @brief Measures sequential backward extended grapheme-cluster traversal.
    void benchmarkBackwardGraphemes(benchmark::State &state, std::string_view text)
    {
        for (auto iteration : state)
        {
            static_cast<void>(iteration);
            std::size_t offset = text.size();
            while (offset > 0)
            {
                Unicode::Types::Utf8BoundaryResult previous = Unicode::Utf8::previousGraphemeBoundary(text, offset);
                if (previous.outcome != Unicode::Types::BoundaryOutcome::Found)
                {
                    state.SkipWithError("Unicode backward grapheme traversal failed.");
                    return;
                }

                offset = previous.byteOffset;
                benchmark::DoNotOptimize(previous.byteOffset);
            }
        }

        state.SetBytesProcessed(state.iterations() * static_cast<std::int64_t>(text.size()));
    }

    /// @brief Measures one stateless next-boundary query deep inside a long Unicode range.
    void benchmarkLocalNextGrapheme(benchmark::State &state, std::string_view text, std::size_t byteOffset)
    {
        for (auto iteration : state)
        {
            static_cast<void>(iteration);
            Unicode::Types::Utf8BoundaryResult next = Unicode::Utf8::nextGraphemeBoundary(text, byteOffset);
            if (next.outcome != Unicode::Types::BoundaryOutcome::Found)
            {
                state.SkipWithError("Unicode local next-grapheme query failed.");
                return;
            }
            benchmark::DoNotOptimize(next.byteOffset);
        }
    }

    /// @brief Measures one stateless previous-boundary query deep inside a long Unicode range.
    void benchmarkLocalPreviousGrapheme(benchmark::State &state, std::string_view text, std::size_t byteOffset)
    {
        for (auto iteration : state)
        {
            static_cast<void>(iteration);
            Unicode::Types::Utf8BoundaryResult previous = Unicode::Utf8::previousGraphemeBoundary(text, byteOffset);
            if (previous.outcome != Unicode::Types::BoundaryOutcome::Found)
            {
                state.SkipWithError("Unicode local previous-grapheme query failed.");
                return;
            }
            benchmark::DoNotOptimize(previous.byteOffset);
        }
    }

    /// @brief Measures one complete caller-indexed forward grapheme walk.
    void benchmarkIndexedForwardGraphemes(benchmark::State &state, std::string_view text)
    {
        std::vector<std::size_t> boundaryStorage(text.size() + 1);

        for (auto iteration : state)
        {
            static_cast<void>(iteration);
            Unicode::Utf8::GraphemeCursor cursor;
            const Unicode::Types::Utf8GraphemeIndexResult indexed = cursor.reset(text, boundaryStorage);
            if (indexed.outcome != Unicode::Types::GraphemeIndexOutcome::Indexed)
            {
                state.SkipWithError("Unicode grapheme cursor indexing failed.");
                return;
            }

            while (cursor.byteOffset() < text.size())
            {
                Unicode::Types::Utf8BoundaryResult next = cursor.next();
                if (next.outcome != Unicode::Types::BoundaryOutcome::Found)
                {
                    state.SkipWithError("Unicode indexed forward grapheme traversal failed.");
                    return;
                }
                benchmark::DoNotOptimize(next.byteOffset);
            }
        }

        state.SetBytesProcessed(state.iterations() * static_cast<std::int64_t>(text.size()));
    }

    /// @brief Measures repeated suffix deletion at indexed grapheme boundaries.
    void benchmarkIndexedBackwardEditing(benchmark::State &state, std::string_view text)
    {
        std::vector<std::size_t> boundaryStorage(text.size() + 1);

        for (auto iteration : state)
        {
            static_cast<void>(iteration);
            std::string editable(text);

            Unicode::Utf8::GraphemeCursor cursor;
            const Unicode::Types::Utf8GraphemeIndexResult indexed = cursor.reset(editable, boundaryStorage);
            if (indexed.outcome != Unicode::Types::GraphemeIndexOutcome::Indexed ||
                cursor.seek(editable.size()).outcome != Unicode::Types::BoundaryOutcome::Found)
            {
                state.SkipWithError("Unicode backward-edit cursor setup failed.");
                return;
            }

            while (!editable.empty())
            {
                const Unicode::Types::Utf8BoundaryResult previous = cursor.previous();
                if (previous.outcome != Unicode::Types::BoundaryOutcome::Found)
                {
                    state.SkipWithError("Unicode indexed backward-edit traversal failed.");
                    return;
                }

                editable.resize(previous.byteOffset);
                cursor.discardAfterCurrent();
                benchmark::DoNotOptimize(editable.size());
            }
        }

        state.SetBytesProcessed(state.iterations() * static_cast<std::int64_t>(text.size()));
    }

    void BM_Unicode_Utf8DecodeAscii(benchmark::State &state)
    {
        benchmarkDecode(state, kAsciiText);
    }

    void BM_Unicode_Utf8DecodeMixed(benchmark::State &state)
    {
        benchmarkDecode(state, kMixedText);
    }

    void BM_Unicode_Utf8ValidateAscii(benchmark::State &state)
    {
        benchmarkValidation(state, kAsciiText);
    }

    void BM_Unicode_Utf8ValidateMixed(benchmark::State &state)
    {
        benchmarkValidation(state, kMixedText);
    }

    /// @brief Measures fixed-result UTF-8 scalar encoding across representative lengths.
    void BM_Unicode_Utf8EncodeScalars(benchmark::State &state)
    {
        for (auto iteration : state)
        {
            static_cast<void>(iteration);
            for (const char32_t scalar : kScalars)
            {
                Unicode::Types::Utf8EncodeResult encoded = Unicode::Utf8::encodeScalar(scalar);
                if (encoded.outcome != Unicode::Types::EncodeOutcome::Encoded)
                {
                    state.SkipWithError("Unicode encode benchmark scalar is invalid.");
                    return;
                }

                benchmark::DoNotOptimize(encoded.bytes.data());
                benchmark::DoNotOptimize(encoded.byteCount);
            }
        }

        state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(kScalars.size()));
    }

    void BM_Unicode_NextCodePointBoundaryMixed(benchmark::State &state)
    {
        benchmarkForwardCodePoints(state, kMixedText);
    }

    void BM_Unicode_PreviousCodePointBoundaryMixed(benchmark::State &state)
    {
        benchmarkBackwardCodePoints(state, kMixedText);
    }

    void BM_Unicode_NextGraphemeBoundaryAscii(benchmark::State &state)
    {
        benchmarkForwardGraphemes(state, kAsciiText);
    }

    void BM_Unicode_NextGraphemeBoundaryComplex(benchmark::State &state)
    {
        benchmarkForwardGraphemes(state, kComplexGraphemeText);
    }

    void BM_Unicode_PreviousGraphemeBoundaryComplex(benchmark::State &state)
    {
        benchmarkBackwardGraphemes(state, kComplexGraphemeText);
    }

    void BM_Unicode_NextGraphemeBoundaryLongComplexLocal(benchmark::State &state)
    {
        const std::string &text = longComplexGraphemeText();
        benchmarkLocalNextGrapheme(state, text, kTraversalBlock.size() * 256 + kLocalNextOffsetInBlock);
    }

    void BM_Unicode_PreviousGraphemeBoundaryLongComplexLocal(benchmark::State &state)
    {
        const std::string &text = longComplexGraphemeText();
        benchmarkLocalPreviousGrapheme(state, text, kTraversalBlock.size() * 256 + kLocalPreviousOffsetInBlock);
    }

    void BM_Unicode_GraphemeCursorForwardLongComplex(benchmark::State &state)
    {
        benchmarkIndexedForwardGraphemes(state, longComplexGraphemeText());
    }

    void BM_Unicode_GraphemeCursorBackwardEditLongComplex(benchmark::State &state)
    {
        benchmarkIndexedBackwardEditing(state, longComplexGraphemeText());
    }

    BENCHMARK(BM_Unicode_Utf8DecodeAscii);
    BENCHMARK(BM_Unicode_Utf8DecodeMixed);
    BENCHMARK(BM_Unicode_Utf8ValidateAscii);
    BENCHMARK(BM_Unicode_Utf8ValidateMixed);
    BENCHMARK(BM_Unicode_Utf8EncodeScalars);
    BENCHMARK(BM_Unicode_NextCodePointBoundaryMixed);
    BENCHMARK(BM_Unicode_PreviousCodePointBoundaryMixed);
    BENCHMARK(BM_Unicode_NextGraphemeBoundaryAscii);
    BENCHMARK(BM_Unicode_NextGraphemeBoundaryComplex);
    BENCHMARK(BM_Unicode_PreviousGraphemeBoundaryComplex);
    BENCHMARK(BM_Unicode_NextGraphemeBoundaryLongComplexLocal);
    BENCHMARK(BM_Unicode_PreviousGraphemeBoundaryLongComplexLocal);
    BENCHMARK(BM_Unicode_GraphemeCursorForwardLongComplex);
    BENCHMARK(BM_Unicode_GraphemeCursorBackwardEditLongComplex);
} // namespace
