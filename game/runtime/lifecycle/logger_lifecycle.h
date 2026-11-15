/// @file logger_lifecycle.h
/// @brief Logger setup, startup diagnostics, and process shutdown for the game runtime.

#pragma once

#include "runtime/options/runtime_options.h"

#include "logger/config.h"

/// @brief Internal Logger lifecycle operations owned by the executable runtime.
namespace GameWIP::Game::Runtime
{
    // ------------------------------------------------------------
    // Logger initialization and startup reports
    // ------------------------------------------------------------

    /// @brief Initializes Console logging using the selected runtime severity and source areas.
    [[nodiscard]] GameWIP::Logger::Types::Init::Result initializeLogger(const Options &options);

    /// @brief Writes detailed Logger initialization failure state to stderr.
    void reportLoggerInitializationFailure(const GameWIP::Logger::Types::Init::Result &result);

    /// @brief Logs invalid runtime settings after Logger is available.
    void reportInvalidOptions(const Options &options);

    /// @brief Logs a concise Logger startup message.
    void logLoggerStartup(const Options &options);

    // ------------------------------------------------------------
    // Logger shutdown
    // ------------------------------------------------------------

    /// @brief Flushes Logger, optionally reports diagnostics, and shuts it down.
    [[nodiscard]] int finishRuntime(int primaryExitCode);
} // namespace GameWIP::Game::Runtime
