/// @file runtime_options.h
/// @brief Runtime settings for logging, Input, and Window diagnostics.

#pragma once

#include "logger/config.h"
#include "input/types.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <optional>
#include <string_view>

#ifndef GAMEWIP_RUNTIME_DEFAULT_LOG_LEVEL_INFO
/// @brief Selects Info instead of Debug as the runtime's default severity.
#define GAMEWIP_RUNTIME_DEFAULT_LOG_LEVEL_INFO 0
#endif

/// @brief Internal configuration for executable runtime logging, Input, and Window diagnostics.
namespace GameWIP::Game::Runtime
{
    namespace Logger = GameWIP::Logger;
    namespace InputTypes = GameWIP::Input::Types;

    /// @brief Build-selected fallback severity when no runtime override is supplied.
    inline constexpr Logger::Types::Level kDefaultLogLevel =
#if GAMEWIP_RUNTIME_DEFAULT_LOG_LEVEL_INFO
        Logger::Types::Level::Info;
#else
        Logger::Types::Level::Debug;
#endif

    /// @brief Default cadence for combined Window and Input pumping.
    inline constexpr std::chrono::nanoseconds kRuntimeLoopInterval{1'000'000'000 / 60};

    /// @brief Settings read once before Logger initialization.
    struct Options
    {
        // Logging selection
        Logger::Types::Level minLevel = kDefaultLogLevel; ///< Minimum severity selected by the build.
        std::string_view logLevelSource = "default";      ///< Source selected for the minimum severity.

        // Diagnostic switches
        bool inputDevices = false; ///< Whether to report bounded device descriptors at Trace.
        bool inputDebug = false;   ///< Whether to report live control changes.
        bool windowEvents = false; ///< Whether to report per-event Window details.

        // Input backend selection
        bool inputBackendsConfigured = false;                   ///< Whether --inputBackends was supplied.
        std::array<InputTypes::BackendKind, 6> inputBackends{}; ///< Optional selected Win32 backends.
        std::size_t inputBackendCount = 0;                      ///< Number of selected backends.

        // Runtime target and cadence
        std::chrono::nanoseconds eventLoopInterval{kRuntimeLoopInterval}; ///< Input and Window pump cadence.
        std::optional<std::size_t> displayIndex;                          ///< Optional zero-based index into enumerated displays.

        // Invalid settings
        std::string_view invalidLogLevel{};                            ///< Invalid value retained for a startup warning.
        std::string_view invalidLogLevelSetting = "GAMEWIP_LOG_LEVEL"; ///< Setting with the invalid value.
        std::string_view invalidInputDevices{};                        ///< Unsupported value supplied to --inputDevices.
        std::string_view invalidInputDebug{};                          ///< Unsupported value supplied to --inputDebug.
        std::string_view invalidWindowEvents{};                        ///< Unsupported value supplied to --windowEvents.
        std::string_view invalidInputBackends{};                       ///< Invalid list supplied to --inputBackends.
        std::string_view invalidEventWait{};                           ///< Invalid environment value, retained for a startup warning.
        std::string_view invalidDisplayIndex{};                        ///< Invalid environment value, retained for a startup warning.
    };

    /// @brief Reads runtime environment settings and command-line overrides.
    /// @details Runtime option names, defaults, and fallback behavior are documented in @ref project_game_executable.
    [[nodiscard]] Options readOptions(int argc, char *const *argv) noexcept;

    /// @brief Returns the stable text name of a Logger severity.
    [[nodiscard]] constexpr std::string_view levelName(Logger::Types::Level level) noexcept
    {
        switch(level)
        {
        case Logger::Types::Level::Trace:
            return "trace";
        case Logger::Types::Level::Debug:
            return "debug";
        case Logger::Types::Level::Info:
            return "info";
        case Logger::Types::Level::Warn:
            return "warn";
        case Logger::Types::Level::Error:
            return "error";
        case Logger::Types::Level::Fatal:
            return "fatal";
        }
        return "unknown";
    }
} // namespace GameWIP::Game::Runtime
