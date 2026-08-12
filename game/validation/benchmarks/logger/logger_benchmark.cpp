/// @file logger_benchmark.cpp
/// @brief Google Benchmark scenarios for Logger producer paths.

#include "logger/logger.h"
#include "test_support/test_support.h"

#include <benchmark/benchmark.h>

#include <array>
#include <chrono>
#include <exception>
#include <format>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>

namespace
{
    namespace Logger = GameWIP::Logger;
    namespace TestSupport = GameWIP::TestSupport;

    constexpr std::string_view source = "LoggerBenchmark";
    constexpr std::string_view message = "logger benchmark message";
    constexpr Logger::Types::SourceId registeredSource = 1;

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

    [[nodiscard]] bool flushCompleted(const Logger::Types::FlushResult &result) noexcept
    {
        return result.status.ok() && result.outcome == Logger::Types::FlushOutcome::Completed;
    }

    class LoggerFixture : public benchmark::Fixture
    {
    protected:
        bool initialize(benchmark::State &state, Logger::Types::Config config, std::string_view directoryName = {})
        {
            static_cast<void>(Logger::shutdown());
            if (!directoryName.empty())
            {
                try
                {
                    workspace_ =
                        std::make_unique<TestSupport::ScopedTemporaryDirectory>(std::string("logger_benchmark_") + std::string(directoryName));
                    if (!workspace_->status().ok())
                    {
                        const std::string error = std::format(
                            "Could not create Logger benchmark workspace: {}.",
                            TestSupport::formatInfrastructureStatus(workspace_->status()));
                        state.SkipWithError(error);
                        workspace_.reset();
                        return false;
                    }
                    directoryText_ = workspace_->path().string();
                    config.logDirectory = directoryText_;
                }
                catch (const std::exception &exception)
                {
                    state.SkipWithError(exception.what());
                    return false;
                }
            }

            const Logger::Types::InitResult initResult = Logger::init(config);
            initialized_ = initResult.status.ok() && initResult.outcome == Logger::Types::InitOutcome::Started;
            if (!initialized_)
            {
                const std::string error = std::format(
                    "Logger initialization failed (status={}, native={}, outputStatus={}, outputNative={}).",
                    static_cast<int>(initResult.status.code),
                    initResult.status.nativeCode,
                    static_cast<int>(initResult.outputSetupStatus.code),
                    initResult.outputSetupStatus.nativeCode);
                state.SkipWithError(error);
            }
            return initialized_;
        }

        void TearDown(benchmark::State &state) override
        {
            if (initialized_)
            {
                const Logger::Types::FlushResult flushResult = Logger::flush(std::chrono::seconds{10});
                const Logger::Types::Stats stats = Logger::getStats();
                static_cast<void>(Logger::shutdown());

                state.counters["queued"] = static_cast<double>(stats.queued);
                state.counters["written"] = static_cast<double>(stats.written);
                state.counters["queue_drops"] = static_cast<double>(stats.queueDropsSoft + stats.queueDropsHard);
                state.counters["peak_queue"] = static_cast<double>(stats.peakQueueDepth);
                if (!flushCompleted(flushResult))
                    state.SkipWithError("Logger flush failed or timed out.");
            }
            initialized_ = false;
            workspace_.reset();
            directoryText_.clear();
        }

        bool initialized_ = false;
        std::unique_ptr<TestSupport::ScopedTemporaryDirectory> workspace_;
        std::string directoryText_;
    };

    class OutputDisabledFixture : public LoggerFixture
    {
    public:
        void SetUp(benchmark::State &state) override
        {
            Logger::Types::Config config = baseConfig();
            config.output = Logger::Types::OutputMode::None;
            // Disabled output is a successful configuration but intentionally does not start a worker.
            const auto init = Logger::init(config);
            if (!init.status.ok() || init.outcome != Logger::Types::InitOutcome::Disabled)
            {
                state.SkipWithError("Logger disabled initialization failed.");
                return;
            }
            initialized_ = true;
        }
    };

    BENCHMARK_DEFINE_F(OutputDisabledFixture, Producer)(benchmark::State &state)
    {
        if (!initialized_)
            return;
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
            config.output = Logger::Types::OutputMode::File;
            config.minLevel = Logger::Types::Level::Fatal;
            initialize(state, config, "filtered");
        }
    };

    BENCHMARK_DEFINE_F(FilteredFormattedFixture, Producer)(benchmark::State &state)
    {
        if (!initialized_)
            return;
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
            config.output = Logger::Types::OutputMode::File;
            config.minLevel = Logger::Types::Level::Info;
            initialize(state, config, "enabled_file");
        }
    };

    BENCHMARK_DEFINE_F(EnabledFileFixture, Producer)(benchmark::State &state)
    {
        if (!initialized_)
            return;
        for (auto iteration : state)
        {
            static_cast<void>(iteration);
            Logger::info(source, message);
        }
        state.SetItemsProcessed(state.iterations());
    }

    class RegisteredSourceFixture : public LoggerFixture
    {
    public:
        void SetUp(benchmark::State &state) override
        {
            const std::array sources{Logger::Types::SourceDefinition{registeredSource, "RegisteredBenchmark"}};
            Logger::Types::Config config = baseConfig();
            config.output = Logger::Types::OutputMode::File;
            config.sources = sources;
            initialize(state, config, "registered_source");
        }
    };

    BENCHMARK_DEFINE_F(RegisteredSourceFixture, Producer)(benchmark::State &state)
    {
        if (!initialized_)
            return;
        for (auto iteration : state)
        {
            static_cast<void>(iteration);
            Logger::info(registeredSource, message);
        }
        state.SetItemsProcessed(state.iterations());
    }

    void multiProducerContention(benchmark::State &state)
    {
        static std::mutex lifecycleMutex;
        static std::size_t activeThreads = 0;
        static bool initialized = false;
        static std::unique_ptr<TestSupport::ScopedTemporaryDirectory> workspace;
        static std::string directoryText;

        {
            const std::lock_guard lock{lifecycleMutex};
            if (activeThreads == 0)
            {
                static_cast<void>(Logger::shutdown());
                try
                {
                    workspace = std::make_unique<TestSupport::ScopedTemporaryDirectory>("logger_benchmark_multi_producer");
                    if (!workspace->status().ok())
                    {
                        state.SkipWithError(
                            std::format(
                                "Could not create multi-producer workspace: {}.",
                                TestSupport::formatInfrastructureStatus(workspace->status())));
                        workspace.reset();
                        initialized = false;
                    }
                    else
                    {
                        directoryText = workspace->path().string();
                        const std::array sources{Logger::Types::SourceDefinition{registeredSource, "RegisteredBenchmark"}};
                        Logger::Types::Config config = baseConfig();
                        config.output = Logger::Types::OutputMode::File;
                        config.logDirectory = directoryText;
                        config.sources = sources;
                        const auto init = Logger::init(config);
                        initialized = init.status.ok() && init.outcome == Logger::Types::InitOutcome::Started;
                    }
                }
                catch (const std::exception &exception)
                {
                    state.SkipWithError(exception.what());
                    initialized = false;
                }
            }
            ++activeThreads;
        }

        if (!initialized)
            state.SkipWithError("Logger initialization failed.");
        else
        {
            for (auto iteration : state)
            {
                static_cast<void>(iteration);
                Logger::info(registeredSource, message);
            }
            state.SetItemsProcessed(state.iterations());
        }

        {
            const std::lock_guard lock{lifecycleMutex};
            --activeThreads;
            if (activeThreads == 0)
            {
                const bool flushed = initialized && flushCompleted(Logger::flush(std::chrono::seconds{10}));
                const Logger::Types::Stats stats = Logger::getStats();
                static_cast<void>(Logger::shutdown());
                workspace.reset();
                directoryText.clear();
                initialized = false;
                state.counters["queued"] = static_cast<double>(stats.queued);
                state.counters["written"] = static_cast<double>(stats.written);
                state.counters["queue_drops"] = static_cast<double>(stats.queueDropsSoft + stats.queueDropsHard);
                state.counters["peak_queue"] = static_cast<double>(stats.peakQueueDepth);
                if (!flushed)
                    state.SkipWithError("Logger flush failed or timed out.");
            }
        }
    }

    BENCHMARK_REGISTER_F(OutputDisabledFixture, Producer)->Name("BM_Logger_OutputDisabled")->UseRealTime();
    BENCHMARK_REGISTER_F(FilteredFormattedFixture, Producer)->Name("BM_Logger_FilteredFormatted")->UseRealTime();
    BENCHMARK_REGISTER_F(EnabledFileFixture, Producer)->Name("BM_Logger_EnabledFile")->UseRealTime();
    BENCHMARK_REGISTER_F(RegisteredSourceFixture, Producer)->Name("BM_Logger_RegisteredSourceId")->UseRealTime();
    BENCHMARK(multiProducerContention)->Name("BM_Logger_MultiProducerContention")->Threads(2)->Threads(4)->Threads(8)->UseRealTime();
} // namespace
