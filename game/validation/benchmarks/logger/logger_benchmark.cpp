/// @file logger_benchmark.cpp
/// @brief Google Benchmark scenarios for Logger producer paths.
///
/// Scenarios isolate producer-path costs and report queue/drop state so measured
/// throughput cannot silently hide discarded asynchronous work.

#include "logger/logger.h"
#include "test_support/test_support.h"

#include <benchmark/benchmark.h>

#include <chrono>
#include <array>
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

    /// @brief Creates deterministic Logger settings shared by all producer benchmarks.
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

    /// @brief Owns Logger lifecycle, temporary file output, and post-run counters outside timed iterations.
    class LoggerFixture : public benchmark::Fixture
    {
    protected:
        /// @brief Initializes Logger and optional temporary file output before timed iterations.
        bool initialize(benchmark::State &state, Logger::Types::Config config, std::string_view directoryName = {})
        {
            Logger::shutdown();
            if (!directoryName.empty())
            {
                try
                {
                    workspace_ =
                        std::make_unique<TestSupport::ScopedTemporaryDirectory>(std::string("logger_benchmark_") + std::string(directoryName));
                    directoryText_ = workspace_->path().string();
                    config.logDirectory = directoryText_;
                }
                catch (const std::exception &exception)
                {
                    state.SkipWithError(exception.what());
                    return false;
                }
            }

            const Logger::Types::Result initResult = Logger::init(config);
            initialized_ = initResult == Logger::Types::Result::Success;
            if (!initialized_)
            {
                const Logger::Types::PlatformError platformError = Logger::getLastPlatformError();
                const std::string error = std::format(
                    "Logger initialization failed (result={}, source={}, native={}).",
                    static_cast<int>(initResult),
                    static_cast<int>(platformError.source),
                    platformError.nativeCode);
                state.SkipWithError(error);
            }
            return initialized_;
        }

        /// @brief Flushes, publishes counters, shuts down, and removes temporary output after timing.
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

            initialized_ = false;
            workspace_.reset();
            directoryText_.clear();
        }

        bool initialized_ = false;
        std::unique_ptr<TestSupport::ScopedTemporaryDirectory> workspace_;
        std::string directoryText_;
    };

    /// @brief Configures the producer path with all output disabled.
    class OutputDisabledFixture : public LoggerFixture
    {
    public:
        /// @brief Starts the disabled-output producer configuration.
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

    /// @brief Configures a file logger that rejects formatted Info messages by severity.
    class FilteredFormattedFixture : public LoggerFixture
    {
    public:
        /// @brief Starts the severity-filtered formatted producer configuration.
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

    /// @brief Configures the accepted asynchronous file-producer path.
    class EnabledFileFixture : public LoggerFixture
    {
    public:
        /// @brief Starts the accepted asynchronous file producer configuration.
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

    /// @brief Configures the registered-SourceId producer path with asynchronous file output.
    class RegisteredSourceFixture : public LoggerFixture
    {
    public:
        /// @brief Starts the registered-source producer configuration.
        void SetUp(benchmark::State &state) override
        {
            const std::array sources{Logger::Types::SourceDefinition{registeredSource, "RegisteredBenchmark"}};
            Logger::Types::Config config = baseConfig();
            config.output = Logger::Types::Output::File;
            config.sources = sources;
            initialize(state, config, "registered_source");
        }
    };

    BENCHMARK_DEFINE_F(RegisteredSourceFixture, Producer)(benchmark::State &state)
    {
        if (!initialized_)
        {
            return;
        }
        for (auto iteration : state)
        {
            static_cast<void>(iteration);
            Logger::info(registeredSource, message);
        }
        state.SetItemsProcessed(state.iterations());
    }

    /// @brief Shares one Logger lifecycle across a Google Benchmark thread group.
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
                Logger::shutdown();
                try
                {
                    workspace = std::make_unique<TestSupport::ScopedTemporaryDirectory>("logger_benchmark_multi_producer");
                    directoryText = workspace->path().string();
                    const std::array sources{Logger::Types::SourceDefinition{registeredSource, "RegisteredBenchmark"}};
                    Logger::Types::Config config = baseConfig();
                    config.output = Logger::Types::Output::File;
                    config.logDirectory = directoryText;
                    config.sources = sources;
                    initialized = Logger::init(config) == Logger::Types::Result::Success;
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
        {
            state.SkipWithError("Logger initialization failed.");
        }
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
                const bool flushed = initialized && Logger::flush(std::chrono::seconds{10});
                const Logger::Types::Stats stats = Logger::getStats();
                Logger::shutdown();
                workspace.reset();
                directoryText.clear();
                initialized = false;

                state.counters["queued"] = static_cast<double>(stats.queued);
                state.counters["written"] = static_cast<double>(stats.written);
                state.counters["queue_drops"] = static_cast<double>(stats.queueDropsSoft + stats.queueDropsHard);
                state.counters["peak_queue"] = static_cast<double>(stats.peakQueueDepth);
                if (!flushed)
                {
                    state.SkipWithError("Logger flush timed out.");
                }
            }
        }
    }

    BENCHMARK_REGISTER_F(OutputDisabledFixture, Producer)->Name("BM_Logger_OutputDisabled")->UseRealTime();
    BENCHMARK_REGISTER_F(FilteredFormattedFixture, Producer)->Name("BM_Logger_FilteredFormatted")->UseRealTime();
    BENCHMARK_REGISTER_F(EnabledFileFixture, Producer)->Name("BM_Logger_EnabledFile")->UseRealTime();
    BENCHMARK_REGISTER_F(RegisteredSourceFixture, Producer)->Name("BM_Logger_RegisteredSourceId")->UseRealTime();
    BENCHMARK(multiProducerContention)->Name("BM_Logger_MultiProducerContention")->Threads(2)->Threads(4)->Threads(8)->UseRealTime();
} // namespace
