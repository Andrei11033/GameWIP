@page logger_quick_start Logger quick start

A minimal setup creates a configuration, initializes the process-wide logger, writes normal async logs, uses reports for important synchronous diagnostics, and shuts down during teardown.

```cpp
#include "logger/logger.h"
#include "logger/logger_macros.h"

int main()
{
    GameWIP::Logger::Types::Config config = GameWIP::Logger::defaultConfig();
    const GameWIP::Logger::Types::Result result = GameWIP::Logger::init(config);
    if (result != GameWIP::Logger::Types::Result::Success)
    {
        return 1;
    }

    GameWIP::Logger::info("Game", "Game started");
    LOGGER_WARN("Assets", "Missing optional asset: {}", "placeholder.png");

    GameWIP::Logger::report(
        GameWIP::Logger::Types::Level::Error,
        "SaveSystem",
        "Failed to write save file");

    GameWIP::Logger::shutdown();
    return 0;
}
```

## Registered source IDs

For hot paths, prefer stable source IDs or unsigned enum source values. The logger copies source names during `init(...)`, so caller-owned `std::string_view` names only need to live through initialization.

```cpp
enum class LogSource : GameWIP::Logger::Types::SourceId
{
    Engine = 1,
    Renderer = 2,
};

const std::array sources{
    GameWIP::Logger::defineSource(LogSource::Engine, "Engine"),
    GameWIP::Logger::defineSource(LogSource::Renderer, "Renderer"),
};

auto config = GameWIP::Logger::defaultConfig();
config.sources = sources;
GameWIP::Logger::init(config);

LOGGER_INFO(LogSource::Engine, "Initialized subsystem {}", "input");
```

## Include rule

Include `logger/logger.h` for the class API. Include `logger/logger_macros.h` only when the global `LOGGER_*` convenience macros are wanted.
