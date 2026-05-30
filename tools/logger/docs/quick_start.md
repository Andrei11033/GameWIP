@page logger_quick_start Logger quick start

A minimal logger setup initializes the process-wide runtime, writes normal async logs, uses reports for important synchronous diagnostics, and shuts down during application teardown.

```cpp
Logger::Types::Config config = Logger::defaultConfig();
Logger::init(config);

Logger::info("Game", "Game started");
Logger::warn("Assets", "Missing optional asset: {}", assetName);

Logger::report(
    Logger::Types::Level::Error,
    "SaveSystem",
    "Failed to write save file"
);

Logger::shutdown();
```

Normal logs are asynchronous, queue-based, and filterable. Reports are synchronous, immediate, filter-bypassing, and non-queued. Use `flush()` before inspecting a log file during a test, and `shutdown()` during final application teardown.

For cheap call sites, direct formatted APIs are fine:

```cpp
Logger::info("Game", "Frame {}", frameIndex);
```

For hot paths or expensive message construction, use `LOGGER_*` macros or `shouldLog()` so message work can be skipped when the level/source is filtered:

```cpp
LOGGER_DEBUG(LogSource::Physics, std::format("contacts {}", contactCount));
```

See @ref logger_lifecycle for startup/shutdown rules and @ref logger_examples for more recipes.
