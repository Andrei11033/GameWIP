/// @file io_benchmark.cpp
/// @brief Memory-backed transfer benchmarks for IO.

#include "io/io.h"

#include <benchmark/benchmark.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace
{
    namespace IO = GameWIP::IO;

    constexpr std::int64_t kSmallPayloadBytes = 4LL * 1024;
    constexpr std::int64_t kLargePayloadBytes = 1024LL * 1024;

    /// @brief Measures repeated fixed-size reads through the virtual Reader contract.
    void BM_IO_MemoryReaderRead(benchmark::State &state)
    {
        const auto payloadSize = static_cast<std::size_t>(state.range(0));
        std::vector<std::byte> payload(payloadSize, std::byte{0x5a});
        std::vector<std::byte> scratch(static_cast<std::size_t>(kSmallPayloadBytes));

        for (auto iteration : state)
        {
            static_cast<void>(iteration);
            IO::MemoryReader reader(payload);
            std::size_t totalRead = 0;
            while (totalRead < payload.size())
            {
                IO::Types::ReadResult result = reader.read(std::span<std::byte>{scratch});
                if (!result.status.ok() || result.bytesRead == 0)
                {
                    state.SkipWithError("MemoryReader benchmark read failed.");
                    return;
                }
                totalRead += result.bytesRead;
            }
            benchmark::DoNotOptimize(scratch.data());
        }

        state.SetBytesProcessed(state.iterations() * static_cast<std::int64_t>(payloadSize));
    }

    /// @brief Measures append into pre-reserved MemoryWriter storage without allocation churn.
    void BM_IO_MemoryWriterWrite(benchmark::State &state)
    {
        const auto payloadSize = static_cast<std::size_t>(state.range(0));
        std::vector<std::byte> payload(payloadSize, std::byte{0xa5});
        IO::MemoryWriter writer;
        if (!writer.reserve(payloadSize).ok())
        {
            state.SkipWithError("MemoryWriter benchmark reserve failed.");
            return;
        }

        for (auto iteration : state)
        {
            static_cast<void>(iteration);
            writer.clear();
            IO::Types::WriteResult result = writer.write(payload);
            if (!result.status.ok() || result.bytesWritten != payload.size())
            {
                state.SkipWithError("MemoryWriter benchmark write failed.");
                return;
            }
            benchmark::DoNotOptimize(writer.bytes().data());
        }

        state.SetBytesProcessed(state.iterations() * static_cast<std::int64_t>(payloadSize));
    }

    /// @brief Measures the known-size whole-read path and its owning result allocation.
    void BM_IO_ReadAllBytesKnownSize(benchmark::State &state)
    {
        const auto payloadSize = static_cast<std::size_t>(state.range(0));
        std::vector<std::byte> payload(payloadSize, std::byte{0x3c});

        for (auto iteration : state)
        {
            static_cast<void>(iteration);
            IO::MemoryReader reader(payload);
            IO::Types::ReadAllBytesResult result = IO::readAllBytes(reader, payloadSize);
            if (!result.status.ok() || result.bytes.size() != payload.size())
            {
                state.SkipWithError("IO known-size read-all benchmark failed.");
                return;
            }
            benchmark::DoNotOptimize(result.bytes.data());
        }

        state.SetBytesProcessed(state.iterations() * static_cast<std::int64_t>(payloadSize));
    }

    BENCHMARK(BM_IO_MemoryReaderRead)->Arg(kSmallPayloadBytes)->Arg(kLargePayloadBytes);
    BENCHMARK(BM_IO_MemoryWriterWrite)->Arg(kSmallPayloadBytes)->Arg(kLargePayloadBytes);
    BENCHMARK(BM_IO_ReadAllBytesKnownSize)->Arg(kSmallPayloadBytes)->Arg(kLargePayloadBytes);
} // namespace
