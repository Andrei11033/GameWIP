/// @file filesystem_benchmark.cpp
/// @brief Directory-enumeration scaling benchmarks for FileSystem.

#include "filesystem/filesystem.h"
#include "test_support/test_support.h"

#include <benchmark/benchmark.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <format>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <utility>

namespace
{
    namespace FileSystem = GameWIP::FileSystem;
    namespace IO = GameWIP::IO;
    namespace TestSupport = GameWIP::TestSupport;

    /// @brief Reuses expensive directory fixtures across materialized and cursor scenarios.
    class DirectoryFixtureStore final
    {
    public:
        /// @brief Returns a populated directory at the requested ancestry depth.
        const std::filesystem::path &directory(int depth, int entryCount)
        {
            const std::lock_guard lock{mutex_};
            const auto key = std::pair{depth, entryCount};
            if (const auto existing = directories_.find(key); existing != directories_.end())
            {
                return existing->second;
            }

            if (!workspace_)
            {
                workspace_ = std::make_unique<TestSupport::ScopedTemporaryDirectory>("filesystem_directory_benchmark");
                if (!workspace_->status().ok())
                {
                    throw std::runtime_error(
                        std::format(
                            "Could not create FileSystem benchmark workspace: {}",
                            TestSupport::formatInfrastructureStatus(workspace_->status())));
                }
            }
            std::filesystem::path directoryPath = workspace_->path() / ("entries_" + std::to_string(entryCount) + "_depth_" + std::to_string(depth));
            for (int level = 0; level < depth; ++level)
            {
                directoryPath /= "d";
            }
            std::filesystem::create_directories(directoryPath);
            for (int index = 0; index < entryCount; ++index)
            {
                std::ofstream file{directoryPath / ("entry_" + std::to_string(index) + ".txt"), std::ios::binary};
                if (!file)
                {
                    throw std::runtime_error("Could not create a FileSystem benchmark fixture.");
                }
            }
            return directories_.emplace(key, std::move(directoryPath)).first->second;
        }

    private:
        std::mutex mutex_;
        std::unique_ptr<TestSupport::ScopedTemporaryDirectory> workspace_;
        std::map<std::pair<int, int>, std::filesystem::path> directories_;
    };

    DirectoryFixtureStore &fixtureStore()
    {
        static DirectoryFixtureStore store;
        return store;
    }

    /// @brief Measures materialized listing across entry counts and supplied path depths.
    void BM_FileSystem_ListDirectory(benchmark::State &state)
    {
        const int depth = static_cast<int>(state.range(0));
        const int entryCount = static_cast<int>(state.range(1));
        const std::filesystem::path *directory = nullptr;
        try
        {
            directory = &fixtureStore().directory(depth, entryCount);
        }
        catch (const std::exception &exception)
        {
            state.SkipWithError(exception.what());
            return;
        }

        for (auto iteration : state)
        {
            static_cast<void>(iteration);
            FileSystem::Types::Directory::ListResult listing = FileSystem::listDirectory(*directory);
            if (!listing.status.ok() || listing.entries.size() != static_cast<std::size_t>(entryCount))
            {
                const std::string error = std::format(
                    "FileSystem listDirectory benchmark enumeration failed (status={}, native={}, entries={}).",
                    IO::errorCodeName(listing.status.code),
                    listing.status.nativeCode,
                    listing.entries.size());
                state.SkipWithError(error);
                break;
            }
            benchmark::DoNotOptimize(listing.entries.data());
        }
        state.SetItemsProcessed(state.iterations() * entryCount);
    }

    /// @brief Measures bounded-memory cursor enumeration over the same directory fixtures.
    void BM_FileSystem_DirectoryCursor(benchmark::State &state)
    {
        const int depth = static_cast<int>(state.range(0));
        const int entryCount = static_cast<int>(state.range(1));
        const std::filesystem::path *directory = nullptr;
        try
        {
            directory = &fixtureStore().directory(depth, entryCount);
        }
        catch (const std::exception &exception)
        {
            state.SkipWithError(exception.what());
            return;
        }

        for (auto iteration : state)
        {
            static_cast<void>(iteration);
            FileSystem::DirectoryCursor cursor;
            const IO::Types::Status openStatus = cursor.open(*directory);
            if (!openStatus.ok())
            {
                const std::string error = std::format(
                    "FileSystem DirectoryCursor benchmark open failed (status={}, native={}).",
                    IO::errorCodeName(openStatus.code),
                    openStatus.nativeCode);
                state.SkipWithError(error);
                break;
            }

            std::int64_t observedEntries = 0;
            for (;;)
            {
                FileSystem::Types::Directory::CursorNextResult next = cursor.next();
                if (!next.status.ok())
                {
                    const std::string error = std::format(
                        "FileSystem DirectoryCursor benchmark enumeration failed (status={}, native={}).",
                        IO::errorCodeName(next.status.code),
                        next.status.nativeCode);
                    state.SkipWithError(error);
                    break;
                }
                if (!next.hasEntry)
                {
                    break;
                }
                ++observedEntries;
                benchmark::DoNotOptimize(next.entry.info.kind);
            }
            if (observedEntries != entryCount)
            {
                state.SkipWithError("FileSystem DirectoryCursor benchmark returned the wrong entry count.");
                break;
            }
        }
        state.SetItemsProcessed(state.iterations() * entryCount);
    }

    void registerDirectoryArguments(benchmark::Benchmark *benchmark)
    {
        for (const std::int64_t depth : {1, 8, 32})
        {
            benchmark->Args({depth, 1'000});
            benchmark->Args({depth, 10'000});
        }
    }

    BENCHMARK(BM_FileSystem_ListDirectory)->Apply(registerDirectoryArguments)->Unit(benchmark::kMillisecond);
    BENCHMARK(BM_FileSystem_DirectoryCursor)->Apply(registerDirectoryArguments)->Unit(benchmark::kMillisecond);
} // namespace
