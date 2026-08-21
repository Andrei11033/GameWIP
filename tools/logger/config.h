/// @file config.h
/// @brief Public Logger configuration model and presets.

#pragma once

#include "logger/logger_export.h"
#include "logger/types.h"

#include <cstddef>
#include <span>
#include <string_view>
#include <type_traits>

namespace GameWIP::Logger
{
    namespace Types
    {
        /// @brief Formatting strategy used before a message enters Logger-owned storage.
        enum class FormatPolicy : std::uint8_t
        {
            StrictBounded, ///< Bound formatting work to configured retained-message storage.
            FastNormal     ///< Favor the normal formatting path and contain allocation failure if it occurs.
        };

        /// @brief Associates a SourceId with its UTF-8 display name during initialization.
        struct SourceDefinition
        {
            SourceId id = 0;            ///< Source identifier.
            std::string_view name = {}; ///< Valid non-empty UTF-8 display name.
        };

        /// @brief Initial enabled state for one registered source.
        struct SourceFilter
        {
            SourceId source = 0; ///< Registered source to configure.
            bool enabled = true; ///< Whether the source is initially enabled.
        };

        /// @brief Initial enabled state for one exact severity level.
        struct LevelFilter
        {
            Level level = Level::Trace; ///< Exact level to configure.
            bool enabled = true;        ///< Whether the level is initially enabled.
        };

        /// @brief Complete Logger initialization configuration.
        struct Config
        {
            OutputMode output = OutputMode::Both;                    ///< Requested normal-output sinks.
            Level minLevel = Level::Info;                            ///< Minimum accepted severity.
            std::size_t maxQueueSize = 1024;                         ///< Soft queue limit.
            double hardQueueMultiplier = 1.25;                       ///< Hard-limit multiplier applied to the soft limit.
            std::size_t maxMessageLength = 4096;                     ///< Maximum retained UTF-8 message bytes.
            FormatPolicy formatPolicy = FormatPolicy::StrictBounded; ///< Formatting memory policy.
            std::size_t inlineMessageCapacity = 256;                 ///< Inline bytes reserved per queued message.
            std::size_t workerBatchSize = 256;                       ///< Maximum records drained per worker batch.
            std::string_view logDirectory = "logs";                  ///< UTF-8 file-output directory.
            bool fallbackToConsoleOnFileFailure = true;              ///< Allow File to fall back to Console.
            std::span<const SourceDefinition> sources = {};          ///< Registered source definitions copied by init().
            std::span<const SourceFilter> sourceFilters = {};        ///< Initial per-source filters.
            std::span<const LevelFilter> levelFilters = {};          ///< Initial exact-level filters.
            bool enableConsoleColor = true;                          ///< Enable supported console styling.
            bool enableDebugOutput = true;                           ///< Mirror reports to the debugger channel.
            bool enableFatalPopup = true;                            ///< Enable the fatal-report popup channel.
            bool flushFileEveryBatch = false;                        ///< Flush File after each worker batch.
            bool flushConsoleEveryWrite = false;                     ///< Flush Console after each normal write.
            bool releaseMessageMemoryAfterWrite = false;             ///< Release excess message scratch after writes.
            bool releaseStorageOnShutdown = true;                    ///< Release queue storage during shutdown.
        };
    } // namespace Types

    /// @cond INTERNAL
    namespace Detail::Core
    {
        template <typename Enum>
        inline constexpr bool isSourceEnum =
            std::is_enum_v<std::remove_cvref_t<Enum>> && !std::is_same_v<std::remove_cvref_t<Enum>, Types::Level> &&
            !std::is_same_v<std::remove_cvref_t<Enum>, Types::OutputMode> && !std::is_same_v<std::remove_cvref_t<Enum>, Types::FormatPolicy> &&
            !std::is_same_v<std::remove_cvref_t<Enum>, Types::Init::Outcome> && !std::is_same_v<std::remove_cvref_t<Enum>, Types::Init::Adjustment> &&
            !std::is_same_v<std::remove_cvref_t<Enum>, Types::FlushOutcome> && !std::is_same_v<std::remove_cvref_t<Enum>, Types::Report::Outcome> &&
            !std::is_same_v<std::remove_cvref_t<Enum>, Types::Report::Delivery> && !std::is_same_v<std::remove_cvref_t<Enum>, Types::Health::State> &&
            !std::is_same_v<std::remove_cvref_t<Enum>, Types::Health::FailureSource>;

        template <typename Enum>
            requires(isSourceEnum<Enum>)
        constexpr Types::SourceId sourceId(Enum value) noexcept
        {
            using Underlying = std::underlying_type_t<std::remove_cvref_t<Enum>>;
            static_assert(std::is_unsigned_v<Underlying>, "Logger source enums must use an unsigned underlying type.");
            static_assert(sizeof(Underlying) <= sizeof(Types::SourceId), "Logger source enum values must fit in SourceId.");
            return static_cast<Types::SourceId>(static_cast<Underlying>(value));
        }
    } // namespace Detail::Core
    /// @endcond

    /// @brief Creates a registered-source definition from an unsigned enum value and UTF-8 name.
    template <typename Enum>
        requires(Detail::Core::isSourceEnum<Enum>)
    constexpr Types::SourceDefinition defineSource(Enum value, std::string_view name) noexcept
    {
        return {Detail::Core::sourceId(value), name};
    }

    /// @brief Returns the balanced default Logger configuration.
    [[nodiscard]] GAMEWIP_LOGGER_EXPORT Types::Config defaultConfig() noexcept;
    /// @brief Returns a configuration favoring lower retained memory.
    [[nodiscard]] GAMEWIP_LOGGER_EXPORT Types::Config lowMemoryConfig() noexcept;
    /// @brief Returns a configuration favoring logging throughput.
    [[nodiscard]] GAMEWIP_LOGGER_EXPORT Types::Config throughputConfig() noexcept;
} // namespace GameWIP::Logger
