/// @file core_types.h
/// @brief Shared Logger implementation types and constants.

#pragma once

#include "logger/logger.h"
#include "filesystem/directory.h"
#include "filesystem/file.h"
#include "io/status.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>

#ifndef LOGGER_INTERNAL_TEST_HOOKS
#define LOGGER_INTERNAL_TEST_HOOKS 0
#endif

namespace GameWIP::Logger::Detail::Core
{
    /// ------------------------------------------------------------
    /// Shared aliases and constants
    /// ------------------------------------------------------------

    using FlushDeadline = std::chrono::steady_clock::time_point;
    using LogLevel = Types::Level;
    using Types::FormatPolicy;
    using Types::LevelFilter;
    using Types::OutputMode;
    using Types::QueueLimits;
    using Types::SourceDefinition;
    using Types::SourceFilter;
    using Types::SourceId;
    using LoggerStats = Types::Stats;
    using LoggerMemoryStats = Types::MemoryStats;
    using FileSystem::FileWriter;
    using IO::Types::ErrorCode;
    using IO::Types::Status;
    using FilePath = FileSystem::Types::Path;

    // Retained text and worker batch defaults
    constexpr std::size_t kInlineSourceCapacity = 64;
    constexpr std::size_t kDefaultInlineMessageCapacity = 256;
    constexpr std::size_t kDefaultWorkerBatchSize = 256;

    // Severity filtering
    constexpr std::size_t kLevelCount = 6;
    constexpr std::uint8_t kAllLevelMask = static_cast<std::uint8_t>((1u << kLevelCount) - 1u);

    // Source lookup
    constexpr std::size_t kInvalidSourceIndex = std::numeric_limits<std::size_t>::max();

    // Packed runtime state
    constexpr std::uint32_t kRuntimeStateRunningBit = 1u << 0u;
    constexpr std::uint32_t kRuntimeStateOutputShift = 1u;
    constexpr std::uint32_t kRuntimeStateMinLevelShift = 4u;
    constexpr std::uint32_t kRuntimeStateLevelMaskShift = 8u;
    constexpr std::uint32_t kRuntimeStateEnumMask = 0x7u;
    constexpr std::uint32_t kRuntimeStateLevelMaskMask = 0x3Fu;

    // Queue publication
    constexpr std::uint32_t kQueueSlotSpinBeforeYield = 64;

} // namespace GameWIP::Logger::Detail::Core
