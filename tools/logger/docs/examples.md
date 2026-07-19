@page logger_examples Examples

The examples use supported public headers and can be adapted directly into an application target linked with Logger.

## Registered sources, filters, and normal logging

```cpp
#include "logger/logger.h"

#include <array>

namespace Logger = GameWIP::Logger;

enum class LogSource : Logger::Types::SourceId
{
    Game = 1,
    Physics = 2,
};

int main()
{
    constexpr std::array sources{
        Logger::defineSource(LogSource::Game, "Game"),
        Logger::defineSource(LogSource::Physics, "Physics"),
    };

    constexpr std::array initialSourceFilters{
        Logger::Types::SourceFilter{
            static_cast<Logger::Types::SourceId>(LogSource::Physics),
            true},
    };

    Logger::Types::Config config = Logger::defaultConfig();
    config.output = Logger::Types::Output::Console;
    config.minLevel = Logger::Types::Level::Debug;
    config.sources = sources;
    config.sourceFilters = initialSourceFilters;

    const Logger::Types::Result result = Logger::init(config);
    if (!Logger::isRunning())
    {
        return result == Logger::Types::Result::Success ? 0 : 1;
    }

    Logger::info(LogSource::Game, "Application started");
    Logger::debug(LogSource::Physics, "Simulation step {}", 42);

    Logger::setSourceFilter(LogSource::Physics, false);
    Logger::debug(LogSource::Physics, "This record is filtered");
    Logger::clearSourceFilter(LogSource::Physics);

    Logger::shutdown();
    return 0;
}
```

The arrays must remain alive until `init()` returns. Logger copies their contents during initialization.

## File output and fallback inspection

```cpp
#include "logger/logger.h"

#include <string>

namespace Logger = GameWIP::Logger;

int main()
{
    Logger::Types::Config config = Logger::defaultConfig();
    config.output = Logger::Types::Output::File;
    config.logDirectory = "logs/session";
    config.fallbackToConsoleOnFileFailure = true;

    const Logger::Types::Result result = Logger::init(config);
    const Logger::Types::Output output = Logger::getOutput();
    const std::string path = Logger::getLogFilePath();

    if (!Logger::isRunning())
    {
        return 1;
    }

    if (output == Logger::Types::Output::Console)
    {
        Logger::warn("Startup", "File output failed; console fallback is active");
    }
    else
    {
        Logger::info("Startup", "Writing log file '{}'", path);
    }

    static_cast<void>(result); // Preserve for application diagnostics.
    Logger::shutdown();
    return 0;
}
```

A non-`Success` result can accompany a usable fallback. Inspect the effective state instead of treating initialization as all-or-nothing.

## Compile-time and runtime formats

```cpp
#include "logger/logger.h"

#include <string>

namespace Logger = GameWIP::Logger;

int main()
{
    Logger::initConsole();

    const int clientId = 7;
    const std::string address = "127.0.0.1";
    Logger::info("Network", "Client {} connected from {}", clientId, address);

    const std::string dynamicFormat = "Client {} disconnected";
    Logger::info("Network", Logger::runtimeFormat(dynamicFormat), clientId);

    Logger::shutdown();
    return 0;
}
```

Prefer compile-time format text. Use `runtimeFormat()` only for text that is genuinely dynamic.

## Lazy expensive diagnostics

```cpp
#include "logger/logger.h"
#include "logger/logger_macros.h"

#include <string>

namespace Logger = GameWIP::Logger;

enum class LogSource : Logger::Types::SourceId
{
    Physics = 1,
};

std::string buildContactGraphDump()
{
    return "body0 -> body1";
}

void logPhysicsState()
{
    LOGGER_DEBUG(
        LogSource::Physics,
        "contact graph {}",
        buildContactGraphDump());
}
```

The source expression runs once. `buildContactGraphDump()` runs only when the active macro's first filter check passes. Register the enum source during initialization when a named source and source filtering are required.

## Synchronous reports and diagnostics

```cpp
#include "logger/logger.h"

#include <chrono>
#include <cstddef>

namespace Logger = GameWIP::Logger;

int main()
{
    Logger::initConsole();

    Logger::reportError("SaveSystem", "Could not write save file");

    const bool drained = Logger::reportFatal(
        "Startup",
        Logger::flushTimeout(std::chrono::milliseconds{250}),
        "Required service failed");

    const Logger::Types::Stats stats = Logger::getStats();
    const Logger::Types::MemoryStats memory = Logger::getMemoryStats();
    const std::size_t lifetimeDrops = Logger::getLifetimeDroppedLogCount();

    static_cast<void>(drained); // Bounded drain result, not report delivery status.
    static_cast<void>(stats);
    static_cast<void>(memory);
    static_cast<void>(lifetimeDrops);

    Logger::shutdown();
    return 0;
}
```

## Fatal termination

```cpp
#include "logger/logger.h"

#include <chrono>
#include <string_view>

namespace Logger = GameWIP::Logger;

[[noreturn]] void failStartup(std::string_view reason)
{
    Logger::fatalTerminate(
        "Startup",
        Logger::flushTimeout(std::chrono::milliseconds{250}),
        "Startup failed: {}",
        reason);
}
```

`fatalTerminate()` calls `std::terminate()` and does not provide normal stack unwinding.
