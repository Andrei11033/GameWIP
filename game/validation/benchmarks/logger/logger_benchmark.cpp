/// @file logger_benchmark.cpp
/// @brief Google Benchmark scenarios for Logger producer paths.

#include "logger/logger.h"

#include <benchmark/benchmark.h>

#include <chrono>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>

namespace
{
    namespace Logger = GameWIP::Logger;

    constexpr std::string_view source = "LoggerBenchmark";
    constexpr std::string_view message = "logger benchmark message";

    Logger::Types::Config baseConfig()
    {
        Logger::Types::Config config;
        config.minLevel = Logger::Types::Level::Trace;
        config.maxQueueSize = 65'536;
        config.hardQueueMultiplier = 2.0;
        config.maxMessageLength = 512;
        config.inlineMessageCapacity = 128;
        config.workerBatchSize = 256;
        config.fallbackToConsoleOnFileFailure = false;
        config.enableConsoleColor = false;
        config.enableDebugOutput = false;
        config.enableFatalPopup = false;
        config.flushFileEveryBatch = false;
        config.flushConsoleEveryWrite = false;
        config.releaseMessageMemoryAfterWrite = false;
        config.releaseStorageOnShutdown = true;
        return config;
    }

    class LoggerFixture : public benchmark::Fixture
    {
    protected:
        bool initialize(benchmark::State &state, Logger::Types::Config config, std::string_view directoryName = {})
        {
            Logger::shutdown();
            if (!directoryName.empty())
            {
                directory_ = std::filesystem::temp_directory_path() / "gamewip_logger_benchmarks" / directoryName;
                std::error_code error;
                std::filesystem::remove_all(directory_, error);
                error.clear();
                std::filesystem::create_directories(directory_, error);
                if (error)
                {
                    state.SkipWithError("Could not create the Logger benchmark directory.");
                    return false;
                }

                directoryText_ = directory_.string();
                config.logDirectory = directoryText_;
            }

            initialized_ = Logger::init(config) == Logger::Types::Result::Success;
            if (!initialized_)
            {
                state.SkipWithError("Logger initialization failed.");
            }
            return initialized_;
        }

        void TearDown(benchmark::State &state) override
        {
            if (initialized_)
            {
                const bool flushed = Logger::flush(std::chrono::seconds{10});
                const Logger::Types::Stats stats = Logger::getStats();
                Logger::shutdown();

                state.counters["queued"] = static_cast<double>(stats.queued);
                state.counters["written"] = static_cast<double>(stats.written);
                state.counters["queue_drops"] = static_cast<double>(stats.queueDropsSoft + stats.queueDropsHard);
                state.counters["peak_queue"] = static_cast<double>(stats.peakQueueDepth);

                if (!flushed)
                {
                    state.SkipWithError("Logger flush timed out.");
                }
            }

            if (!directory_.empty())
            {
                std::error_code error;
                std::filesystem::remove_all(directory_, error);
            }
        }

        bool initialized_ = false;
        std::filesystem::path directory_;
        std::string directoryText_;
    };

    class OutputDisabledFixture : public LoggerFixture
    {
    public:
        void SetUp(benchmark::State &state) override
        {
            Logger::Types::Config config = baseConfig();
            config.output = Logger::Types::Output::None;
            initialize(state, config);
        }
    };

    BENCHMARK_DEFINE_F(OutputDisabledFixture, Producer)(benchmark::State &state)
    {
        if (!initialized_)
        {
            return;
        }
        for (auto iteration : state)
        {
            static_cast<void>(iteration);
            Logger::info(source, message);
        }
        state.SetItemsProcessed(state.iterations());
    }

    class FilteredFormattedFixture : public LoggerFixture
    {
    public:
        void SetUp(benchmark::State &state) override
        {
            Logger::Types::Config config = baseConfig();
            config.output = Logger::Types::Output::File;
            config.minLevel = Logger::Types::Level::Fatal;
            initialize(state, config, "filtered");
        }
    };

    BENCHMARK_DEFINE_F(FilteredFormattedFixture, Producer)(benchmark::State &state)
    {
        if (!initialized_)
        {
            return;
        }
        std::size_t value = 0;
        for (auto iteration : state)
        {
            static_cast<void>(iteration);
            Logger::info(source, "filtered {} {}", value, value + 1);
            ++value;
        }
        state.SetItemsProcessed(state.iterations());
    }

    class EnabledFileFixture : public LoggerFixture
    {
    public:
        void SetUp(benchmark::State &state) override
        {
            Logger::Types::Config config = baseConfig();
            config.output = Logger::Types::Output::File;
            config.minLevel = Logger::Types::Level::Info;
            initialize(state, config, "enabled_file");
        }
    };

    BENCHMARK_DEFINE_F(EnabledFileFixture, Producer)(benchmark::State &state)
    {
        if (!initialized_)
        {
            return;
        }
        for (auto iteration : state)
        {
            static_cast<void>(iteration);
            Logger::info(source, message);
        }
        state.SetItemsProcessed(state.iterations());
    }

    BENCHMARK_REGISTER_F(OutputDisabledFixture, Producer)->Name("BM_Logger_OutputDisabled")->UseRealTime();
    BENCHMARK_REGISTER_F(FilteredFormattedFixture, Producer)->Name("BM_Logger_FilteredFormatted")->UseRealTime();
    BENCHMARK_REGISTER_F(EnabledFileFixture, Producer)->Name("BM_Logger_EnabledFile")->UseRealTime();
} // namespace
