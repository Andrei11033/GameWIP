/// @file core_types.h
/// @brief Shared Logger implementation types and constants.

#pragma once

#include "logger/config.h"
#include "filesystem/directory.h"
#include "filesystem/file.h"
#include "status/status.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>

#ifndef LOGGER_INTERNAL_TEST_HOOKS
#define LOGGER_INTERNAL_TEST_HOOKS 0
#endif

namespace GameWIP::Logger::Internal::Core
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
    using ErrorCode = GameWIP::Status::Types::Code;
    using GameWIP::Status::Types::Status;
    using FilePath = FileSystem::Types::Path;

    /// @brief Creates a code-only Logger status without allocating diagnostics.
    [[nodiscard]] inline Status makeStatus(ErrorCode code) noexcept
    {
        return Status(code);
    }

    /// @brief Adds an unclassified backend code as optional diagnostic detail.
    [[nodiscard]] inline Status makeStatus(ErrorCode code, std::int64_t nativeCode) noexcept
    {
        if(nativeCode == 0)
        {
            return Status(code);
        }

        return Status::withDiagnostic(code, {GameWIP::Status::Types::NativeCodeDomain::Other, nativeCode});
    }

    /// @brief Converts an internal operation status to Logger's public result shape.
    [[nodiscard]] inline GameWIP::Status::Types::Result<void> resultFromStatus(Status status) noexcept
    {
        if(status.ok())
        {
            return GameWIP::Status::Types::Result<void>::success();
        }

        return GameWIP::Status::Types::Result<void>::failure(std::move(status));
    }

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

} // namespace GameWIP::Logger::Internal::Core
