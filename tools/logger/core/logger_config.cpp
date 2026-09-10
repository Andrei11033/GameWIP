/// @file logger_config.cpp
/// @brief Logger public configuration presets.

#include "logger/logger.h"

// ------------------------------------------------------------
// Configuration presets
// ------------------------------------------------------------

GameWIP::Logger::Types::Config GameWIP::Logger::defaultConfig() noexcept
{
    return {};
}

GameWIP::Logger::Types::Config GameWIP::Logger::lowMemoryConfig() noexcept
{
    Types::Config config;

    config.maxQueueSize = 256;
    config.hardQueueMultiplier = 1.0;
    config.maxMessageLength = 1024;
    config.inlineMessageCapacity = 128;
    config.workerBatchSize = 64;

    config.releaseMessageMemoryAfterWrite = true;
    config.releaseStorageOnShutdown = true;

    return config;
}

GameWIP::Logger::Types::Config GameWIP::Logger::throughputConfig() noexcept
{
    Types::Config config;

    config.maxQueueSize = 4096;
    config.hardQueueMultiplier = 1.25;
    config.maxMessageLength = 4096;
    config.inlineMessageCapacity = 256;
    config.workerBatchSize = 512;

    config.releaseMessageMemoryAfterWrite = false;
    config.releaseStorageOnShutdown = false;

    return config;
}
