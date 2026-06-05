/// @file logger_filters.cpp
/// @brief Logger runtime filtering, source registration, and source lookup helpers.

#include "logger/internal/logger_core.h"

namespace GameWIP::Logger::Detail::Core
{
    //-------------------------------------------------------------------------------------------------
    // Runtime filtering and source lookup
    //-------------------------------------------------------------------------------------------------

    /// @brief Loads the current shared source registry snapshot for lifecycle-safe lookup.
    /// @return Shared source registry snapshot, or nullptr when no sources are registered.
    std::shared_ptr<SourceRegistry> loadSourceRegistry()
    {
        return loggerState().sourceRegistry.load(std::memory_order_acquire);
    }

    /// @brief Finds a registered source by ID in a source registry snapshot.
    /// @param registry Source registry snapshot to search.
    /// @param source SourceId to find.
    /// @return Pointer to source metadata, or nullptr when the ID is unknown.
    RegisteredSource *findSource(SourceRegistry &registry, SourceId source)
    {
        if (!registry.directSourceLookup.empty() && source >= registry.directSourceBase)
        {
            const SourceId offset = source - registry.directSourceBase;
            if (offset < registry.directSourceLookup.size())
            {
                const std::size_t index = registry.directSourceLookup[offset];
                if (index != kInvalidSourceIndex)
                {
                    return &registry.sources[index];
                }
            }
        }

        const auto position = std::lower_bound(
            registry.sources.begin(),
            registry.sources.end(),
            source,
            [](const RegisteredSource &candidate, SourceId id)
            {
                return candidate.id < id;
            });

        if (position != registry.sources.end() && position->id == source)
        {
            return &(*position);
        }

        return nullptr;
    }

    /// @brief Finds a registered source by ID in a source registry snapshot.
    /// @param registry Source registry snapshot to search.
    /// @param source SourceId to find.
    /// @return Pointer to source metadata, or nullptr when the ID is unknown.
    const RegisteredSource *findSource(const SourceRegistry &registry, SourceId source)
    {
        if (!registry.directSourceLookup.empty() && source >= registry.directSourceBase)
        {
            const SourceId offset = source - registry.directSourceBase;
            if (offset < registry.directSourceLookup.size())
            {
                const std::size_t index = registry.directSourceLookup[offset];
                if (index != kInvalidSourceIndex)
                {
                    return &registry.sources[index];
                }
            }
        }

        const auto position = std::lower_bound(
            registry.sources.begin(),
            registry.sources.end(),
            source,
            [](const RegisteredSource &candidate, SourceId id)
            {
                return candidate.id < id;
            });

        if (position != registry.sources.end() && position->id == source)
        {
            return &(*position);
        }

        return nullptr;
    }

    /// @brief Rebuilds compact direct source lookup when registered IDs are dense enough.
    /// @param registry Source registry with a sorted source table.
    void rebuildSourceLookup(SourceRegistry &registry)
    {
        registry.directSourceLookup.clear();
        registry.directSourceBase = 0;
        if (registry.sources.empty())
        {
            return;
        }

        const SourceId minSource = registry.sources.front().id;
        const SourceId maxSource = registry.sources.back().id;
        const std::uint64_t span64 = static_cast<std::uint64_t>(maxSource) - static_cast<std::uint64_t>(minSource) + 1u;
        const std::uint64_t densityLimit = std::max<std::uint64_t>(64u, static_cast<std::uint64_t>(registry.sources.size()) * 4u);
        if (span64 > densityLimit || span64 > 4096u)
        {
            return;
        }

        registry.directSourceBase = minSource;
        registry.directSourceLookup.assign(static_cast<std::size_t>(span64), kInvalidSourceIndex);
        for (std::size_t index = 0; index < registry.sources.size(); ++index)
        {
            registry.directSourceLookup[registry.sources[index].id - minSource] = index;
        }
    }

    /// @brief Checks the runtime source filter.
    /// @param source SourceId to test.
    /// @return True when the source is unknown or registered and enabled.
    /// @note Unknown sources intentionally log as UnknownSource instead of dropping.
    bool sourceEnabledRuntime(SourceId source)
    {
        const std::shared_ptr<SourceRegistry> registry = loadSourceRegistry();
        return ::GameWIP::Logger::Detail::Core::sourceEnabledRuntime(registry.get(), source);
    }

    /// @brief Checks the runtime source filter using a caller-owned registry snapshot.
    /// @param registry Source registry snapshot, or nullptr when no sources are registered.
    /// @param source SourceId to test.
    /// @return True when the source is unknown or registered and enabled.
    bool sourceEnabledRuntime(const SourceRegistry *registry, SourceId source)
    {
        if (!registry)
        {
            return true;
        }

        const RegisteredSource *registeredSource = findSource(*registry, source);
        return registeredSource == nullptr || registeredSource->enabled.load(std::memory_order_acquire);
    }

    /// @brief Hot-path severity-only log gate used before formatting/allocation.
    /// @param level Level to test.
    /// @return True when the logger is running, output is enabled, minLevel passes, and the level is enabled.
    bool shouldLogRuntime(LogLevel level)
    {
        if (!isValidLevel(level))
        {
            return false;
        }

        const std::uint32_t runtimeState = loggerState().runtimeStateBits.load(std::memory_order_acquire);
        if ((runtimeState & kRuntimeStateRunningBit) == 0)
        {
            return false;
        }

        if (runtimeStateOutput(runtimeState) == OutputMode::None)
        {
            return false;
        }

        if (toLevelValue(level) < toLevelValue(runtimeStateMinLevel(runtimeState)))
        {
            return false;
        }

        const std::uint8_t bit = levelBit(level);
        return bit != 0 && (runtimeStateLevelMask(runtimeState) & bit) != 0;
    }

    /// @brief Hot-path registered source log gate used before formatting/allocation.
    /// @param level Level to test.
    /// @param source SourceId to test.
    /// @return True when shouldLogRuntime(level) passes and the source filter allows the source.
    bool shouldLogRuntime(LogLevel level, SourceId source)
    {
        return ::GameWIP::Logger::Detail::Core::shouldLogRuntime(level) && ::GameWIP::Logger::Detail::Core::sourceEnabledRuntime(source);
    }

    /// @brief Rechecks a pending entry against the current packed runtime state.
    /// @param entry Entry to test.
    /// @return Accept/drop reason for the enqueue path.
    /// @note This second check closes the race where filters change after formatting but before enqueue.
    FilterDecision checkPendingEntryAcceptedUnlocked(const PendingLogEntry &entry)
    {
        const std::uint32_t runtimeState = loggerState().runtimeStateBits.load(std::memory_order_acquire);
        if (!isValidLevel(entry.level) || (runtimeState & kRuntimeStateRunningBit) == 0 || runtimeStateOutput(runtimeState) == OutputMode::None)
        {
            return {};
        }

        if (entry.bypassFilters)
        {
            return {true};
        }

        if (toLevelValue(entry.level) < toLevelValue(runtimeStateMinLevel(runtimeState)))
        {
            return {};
        }

        const std::uint8_t levelMaskBit = levelBit(entry.level);
        if (levelMaskBit == 0 || (runtimeStateLevelMask(runtimeState) & levelMaskBit) == 0)
        {
            return {};
        }

        if (entry.usesRegisteredSource && !::GameWIP::Logger::Detail::Core::sourceEnabledRuntime(entry.sourceId))
        {
            return {};
        }

        return {true};
    }

    /// @brief Copies, sorts, validates, and initially filters source definitions.
    /// @param definitions Source definitions supplied by Config.
    /// @param filters Initial source filters supplied by Config.
    /// @param registry Output source registry snapshot.
    /// @param outResult Receives the specific validation error on failure.
    /// @return True when all source definitions and filters are valid.
    bool prepareSources(
        std::span<const SourceDefinition> definitions,
        std::span<const SourceFilter> filters,
        std::shared_ptr<SourceRegistry> &registry,
        LoggerResult &outResult)
    {
        try
        {
            registry.reset();
            if (definitions.empty() && filters.empty())
            {
                return true;
            }

            std::shared_ptr<SourceRegistry> preparedRegistry = std::make_shared<SourceRegistry>();
            std::vector<RegisteredSource> &sources = preparedRegistry->sources;
            sources.reserve(definitions.size());
            for (const SourceDefinition &definition : definitions)
            {
                if (definition.name.empty())
                {
                    outResult = LoggerResult::InvalidSourceDefinition;
                    return false;
                }

                sources.emplace_back(definition.id, std::string(definition.name), true);
            }

            std::sort(
                sources.begin(),
                sources.end(),
                [](const RegisteredSource &left, const RegisteredSource &right)
                {
                    return left.id < right.id;
                });

            const auto duplicateSource = std::adjacent_find(
                sources.begin(),
                sources.end(),
                [](const RegisteredSource &left, const RegisteredSource &right)
                {
                    return left.id == right.id;
                });
            if (duplicateSource != sources.end())
            {
                outResult = LoggerResult::InvalidSourceDefinition;
                return false;
            }

            std::vector<SourceId> filteredSources;
            filteredSources.reserve(filters.size());
            for (const SourceFilter &filter : filters)
            {
                auto sourcePosition = std::lower_bound(
                    sources.begin(),
                    sources.end(),
                    filter.source,
                    [](const RegisteredSource &candidate, SourceId id)
                    {
                        return candidate.id < id;
                    });

                if (sourcePosition == sources.end() || sourcePosition->id != filter.source)
                {
                    outResult = LoggerResult::InvalidSourceFilter;
                    return false;
                }

                filteredSources.push_back(filter.source);
                sourcePosition->enabled.store(filter.enabled, std::memory_order_relaxed);
            }

            std::sort(filteredSources.begin(), filteredSources.end());
            const auto duplicateFilter = std::adjacent_find(filteredSources.begin(), filteredSources.end());
            if (duplicateFilter != filteredSources.end())
            {
                outResult = LoggerResult::InvalidSourceFilter;
                return false;
            }

            ::GameWIP::Logger::Detail::Core::rebuildSourceLookup(*preparedRegistry);
            registry = std::move(preparedRegistry);
            return true;
        }
        catch (...)
        {
            registry.reset();
            outResult = LoggerResult::InvalidSourceDefinition;
            return false;
        }
    }

    /// @brief Builds the initial runtime level-filter bitmask from Config.
    /// @param filters Initial level filters supplied by Config.
    /// @param outMask Receives the resulting enabled-level bitmask.
    /// @return True when all level filters are valid and non-duplicated.
    bool prepareLevelMask(std::span<const LevelFilter> filters, std::uint8_t &outMask)
    {
        outMask = kAllLevelMask;
        std::uint8_t seenLevels = 0;
        for (const LevelFilter &filter : filters)
        {
            const std::uint8_t bit = levelBit(filter.level);
            if (bit == 0 || (seenLevels & bit) != 0)
            {
                return false;
            }

            seenLevels |= bit;
            if (filter.enabled)
            {
                outMask = static_cast<std::uint8_t>(outMask | bit);
            }
            else
            {
                outMask = static_cast<std::uint8_t>(outMask & ~bit);
            }
        }

        return true;
    }
} // namespace GameWIP::Logger::Detail::Core

//-------------------------------------------------------------------------------------------------
// Public filter API
//-------------------------------------------------------------------------------------------------

/// @brief Tests whether a severity-only log would currently be accepted.
/// @param level Level to test.
/// @return True when output/running/minLevel/runtime-level filters allow the log.
bool GameWIP::Logger::shouldLog(Types::Level level)
{
    return ::GameWIP::Logger::Detail::Core::shouldLogRuntime(level);
}

/// @brief Tests whether a string source log would currently be accepted.
bool GameWIP::Logger::shouldLog(Types::Level level, std::string_view)
{
    return ::GameWIP::Logger::Detail::Core::shouldLogRuntime(level);
}

/// @brief Tests whether a registered source log would currently be accepted.
/// @param level Level to test.
/// @param source SourceId to test.
/// @return True when shouldLog(level) passes and runtime source filters allow the source.
bool GameWIP::Logger::shouldLog(Types::Level level, Types::SourceId source)
{
    return ::GameWIP::Logger::Detail::Core::shouldLogRuntime(level, source);
}

/// @brief Enables or disables one registered source at runtime.
/// @param source SourceId to change.
/// @param enabled True to enable the source, false to filter it out.
/// @return Success or InvalidSourceFilter for unknown sources.
GameWIP::Logger::Types::Result GameWIP::Logger::setSourceFilter(Types::SourceId source, bool enabled)
{
    std::lock_guard<std::mutex> lock(::GameWIP::Logger::Detail::Core::loggerState().logMutex);
    const std::shared_ptr<::GameWIP::Logger::Detail::Core::SourceRegistry> registry = ::GameWIP::Logger::Detail::Core::loadSourceRegistry();
    const ::GameWIP::Logger::Detail::Core::RegisteredSource *registeredSource =
        registry ? ::GameWIP::Logger::Detail::Core::findSource(*registry, source) : nullptr;
    if (registeredSource == nullptr)
    {
        ::GameWIP::Logger::Detail::Core::setResultUnlocked(Types::Result::InvalidSourceFilter);
        return Types::Result::InvalidSourceFilter;
    }

    registeredSource->enabled.store(enabled, std::memory_order_release);
    ::GameWIP::Logger::Detail::Core::setResultUnlocked(Types::Result::Success);
    return Types::Result::Success;
}

/// @brief Clears one source filter by enabling that source.
/// @param source SourceId to enable.
/// @return Success or InvalidSourceFilter for unknown sources.
GameWIP::Logger::Types::Result GameWIP::Logger::clearSourceFilter(Types::SourceId source)
{
    return setSourceFilter(source, true);
}

/// @brief Clears all source filters by enabling every registered source.
void GameWIP::Logger::clearSourceFilters()
{
    std::lock_guard<std::mutex> lock(::GameWIP::Logger::Detail::Core::loggerState().logMutex);
    const std::shared_ptr<::GameWIP::Logger::Detail::Core::SourceRegistry> registry = ::GameWIP::Logger::Detail::Core::loadSourceRegistry();
    if (!registry)
    {
        ::GameWIP::Logger::Detail::Core::setResultUnlocked(Types::Result::Success);
        return;
    }

    for (const ::GameWIP::Logger::Detail::Core::RegisteredSource &source : registry->sources)
    {
        source.enabled.store(true, std::memory_order_release);
    }
    ::GameWIP::Logger::Detail::Core::setResultUnlocked(Types::Result::Success);
}

/// @brief Enables or disables one exact severity level at runtime.
/// @param level Exact level to change.
/// @param enabled True to enable the level, false to filter it out.
/// @return Success or InvalidLevelFilter for invalid enum values.
GameWIP::Logger::Types::Result GameWIP::Logger::setLevelFilter(Types::Level level, bool enabled)
{
    const std::uint8_t bit = ::GameWIP::Logger::Detail::Core::levelBit(level);
    if (bit == 0)
    {
        ::GameWIP::Logger::Detail::Core::recordResult(Types::Result::InvalidLevelFilter);
        return Types::Result::InvalidLevelFilter;
    }

    std::lock_guard<std::mutex> lock(::GameWIP::Logger::Detail::Core::loggerState().logMutex);
    if (enabled)
    {
        ::GameWIP::Logger::Detail::Core::loggerState().enabledLevelMask =
            static_cast<std::uint8_t>(::GameWIP::Logger::Detail::Core::loggerState().enabledLevelMask | bit);
    }
    else
    {
        ::GameWIP::Logger::Detail::Core::loggerState().enabledLevelMask =
            static_cast<std::uint8_t>(::GameWIP::Logger::Detail::Core::loggerState().enabledLevelMask & static_cast<std::uint8_t>(~bit));
    }

    ::GameWIP::Logger::Detail::Core::publishRuntimeStateUnlocked();
    ::GameWIP::Logger::Detail::Core::setResultUnlocked(Types::Result::Success);
    return Types::Result::Success;
}

/// @brief Clears one level filter by enabling that exact level.
/// @param level Exact level to enable.
/// @return Success or InvalidLevelFilter for invalid enum values.
GameWIP::Logger::Types::Result GameWIP::Logger::clearLevelFilter(Types::Level level)
{
    return setLevelFilter(level, true);
}

/// @brief Clears all level filters by enabling every valid level.
void GameWIP::Logger::clearLevelFilters()
{
    std::lock_guard<std::mutex> lock(::GameWIP::Logger::Detail::Core::loggerState().logMutex);
    ::GameWIP::Logger::Detail::Core::loggerState().enabledLevelMask = ::GameWIP::Logger::Detail::Core::kAllLevelMask;
    ::GameWIP::Logger::Detail::Core::publishRuntimeStateUnlocked();
    ::GameWIP::Logger::Detail::Core::setResultUnlocked(Types::Result::Success);
}
