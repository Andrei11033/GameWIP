/// @file logger_header.cpp
/// @brief Logger public-header self-containment compile check.
///
/// This translation unit intentionally includes only `logger/logger.h` first. This proves
/// the installed public header can be parsed without relying on include order
/// from another GameWIP header.

#include "logger/logger.h"

#include <type_traits>

static_assert(noexcept(GameWIP::Logger::getMinLevel()));
static_assert(noexcept(GameWIP::Logger::getOutput()));
static_assert(noexcept(GameWIP::Logger::getLogFilePath()));
static_assert(noexcept(GameWIP::Logger::getQueueLimits()));
static_assert(noexcept(GameWIP::Logger::getLifetimeDroppedLogCount()));
static_assert(noexcept(GameWIP::Logger::getHealth()));
static_assert(noexcept(GameWIP::Logger::getStats()));
static_assert(noexcept(GameWIP::Logger::getMemoryStats()));
static_assert(noexcept(GameWIP::Logger::resetStats()));
static_assert(std::is_nothrow_move_constructible_v<GameWIP::Logger::Types::LogFilePathResult>);
