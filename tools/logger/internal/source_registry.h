/// @file source_registry.h
/// @brief Logger source metadata and lookup storage.

#pragma once

#include "logger/internal/core_types.h"

#include <atomic>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace GameWIP::Logger::Internal::Core
{
    /// ------------------------------------------------------------
    /// Registered sources
    /// ------------------------------------------------------------

    struct RegisteredSource
    {
        // Stable identity and display
        SourceId id = 0;
        std::string name;
        std::optional<Color> color;

        // Mutable runtime filter
        mutable std::atomic<bool> enabled{true};

        // Construction and assignment
        RegisteredSource() = default;
        RegisteredSource(SourceId source, std::string displayName, std::optional<Color> sourceColor, bool sourceEnabled)
            : id(source)
            , name(std::move(displayName))
            , color(sourceColor)
            , enabled(sourceEnabled)
        {
        }

        RegisteredSource(const RegisteredSource &other)
            : id(other.id)
            , name(other.name)
            , color(other.color)
            , enabled(other.enabled.load(std::memory_order_relaxed))
        {
        }

        RegisteredSource(RegisteredSource &&other) noexcept
            : id(other.id)
            , name(std::move(other.name))
            , color(other.color)
            , enabled(other.enabled.load(std::memory_order_relaxed))
        {
        }

        RegisteredSource &operator=(const RegisteredSource &other)
        {
            if(this != &other)
            {
                id = other.id;
                name = other.name;
                color = other.color;
                enabled.store(other.enabled.load(std::memory_order_relaxed), std::memory_order_relaxed);
            }

            return *this;
        }

        RegisteredSource &operator=(RegisteredSource &&other) noexcept
        {
            if(this != &other)
            {
                id = other.id;
                name = std::move(other.name);
                color = other.color;
                enabled.store(other.enabled.load(std::memory_order_relaxed), std::memory_order_relaxed);
            }

            return *this;
        }
    };

    /// ------------------------------------------------------------
    /// Source registry snapshot
    /// ------------------------------------------------------------

    struct SourceRegistry
    {
        // Sorted source definitions
        std::vector<RegisteredSource> sources;

        // Optional dense lookup table for registered IDs
        std::vector<std::size_t> directSourceLookup;
        SourceId directSourceBase = 0;
    };

} // namespace GameWIP::Logger::Internal::Core
