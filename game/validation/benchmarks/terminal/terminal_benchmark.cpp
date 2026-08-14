/// @file terminal_benchmark.cpp
/// @brief Diagnostic benchmarks for checked Terminal caller-owned output buffering.

#include "terminal/terminal.h"

#include <benchmark/benchmark.h>

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace
{
    namespace Terminal = GameWIP::Terminal;

    constexpr std::string_view kChunk = "GameWIP Terminal checked buffer payload ";

    /// @brief Measures the checked plain append hot path while reusing retained capacity.
    void BM_Terminal_OutputBufferAppend(benchmark::State &state)
    {
        Terminal::OutputBuffer buffer;
        if (!buffer.reserve(kChunk.size() * 64).ok())
        {
            state.SkipWithError("Terminal OutputBuffer reserve failed.");
            return;
        }

        for (auto iteration : state)
        {
            static_cast<void>(iteration);
            buffer.clear();

            for (std::size_t index = 0; index < 64; ++index)
            {
                if (!buffer.appendText(kChunk).ok())
                {
                    state.SkipWithError("Terminal OutputBuffer append failed.");
                    return;
                }
            }

            benchmark::DoNotOptimize(buffer.text().data());
            benchmark::ClobberMemory();
        }

        state.SetBytesProcessed(state.iterations() * static_cast<std::int64_t>(kChunk.size() * 64));
    }

    /// @brief Measures rollback-capable formatting into retained OutputBuffer storage.
    void BM_Terminal_OutputBufferFormat(benchmark::State &state)
    {
        Terminal::OutputBuffer buffer;
        if (!buffer.reserve(4096).ok())
        {
            state.SkipWithError("Terminal OutputBuffer reserve failed.");
            return;
        }

        for (auto iteration : state)
        {
            static_cast<void>(iteration);
            buffer.clear();

            for (std::uint32_t index = 0; index < 64; ++index)
            {
                if (!buffer.print("frame={} entity={} ", index, index * 3U).ok())
                {
                    state.SkipWithError("Terminal OutputBuffer formatting failed.");
                    return;
                }
            }

            benchmark::DoNotOptimize(buffer.text().data());
            benchmark::ClobberMemory();
        }
    }

    /// @brief Measures checked line batching without releasing caller-owned capacity between records.
    void BM_Terminal_OutputBufferLines(benchmark::State &state)
    {
        Terminal::OutputBuffer buffer;
        if (!buffer.setLineEnding(Terminal::Types::Output::LineEnding::Lf).ok() || !buffer.reserve(4096).ok())
        {
            state.SkipWithError("Terminal OutputBuffer setup failed.");
            return;
        }

        for (auto iteration : state)
        {
            static_cast<void>(iteration);
            buffer.clear();

            for (std::size_t index = 0; index < 64; ++index)
            {
                if (!buffer.appendLine(kChunk).ok())
                {
                    state.SkipWithError("Terminal OutputBuffer line append failed.");
                    return;
                }
            }

            benchmark::DoNotOptimize(buffer.text().data());
            benchmark::ClobberMemory();
        }

        state.SetBytesProcessed(state.iterations() * static_cast<std::int64_t>((kChunk.size() + 1) * 64));
    }
} // namespace

BENCHMARK(BM_Terminal_OutputBufferAppend);
BENCHMARK(BM_Terminal_OutputBufferFormat);
BENCHMARK(BM_Terminal_OutputBufferLines);
