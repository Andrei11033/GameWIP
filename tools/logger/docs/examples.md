@page logger_examples Logger examples

Examples use this namespace alias for readability:

```cpp
namespace Logger = GameWIP::Logger;
```

## Initialize with a custom config

```cpp
auto config = Logger::defaultConfig();
config.output = Logger::Types::Output::Both;
config.minLevel = Logger::Types::Level::Info;
config.logDirectory = "logs";
Logger::init(config);
```

## Register source IDs

```cpp
enum class LogSource : Logger::Types::SourceId
{
    Game = 1,
    Physics = 2,
};

auto config = Logger::defaultConfig();
config.sources = {
    Logger::defineSource(LogSource::Game, "Game"),
    Logger::defineSource(LogSource::Physics, "Physics"),
};
Logger::init(config);
Logger::info(LogSource::Game, "Started");
```

## Normal logging

```cpp
Logger::info("Game", "Loaded {} assets", assetCount);
Logger::debug("Physics", "Step took {} ms", stepMs);
```

## Runtime format string

```cpp
std::string format = "{} connected from {}";
Logger::info("Network", Logger::runtimeFormat(format), playerName, address);
```

Use `runtimeFormat` only when the format string cannot be compile-time checked.

## Lazy macro

```cpp
LOGGER_DEBUG(LogSource::Physics, std::format("contact graph {}", buildContactGraphDump()));
```

The message expression is skipped when Debug or the source is filtered.

## Runtime filters

```cpp
Logger::setLevelFilter(Logger::Types::Level::Debug, false);
Logger::setSourceFilter(LogSource::Physics, false);
Logger::clearLevelFilters();
Logger::clearSourceFilters();
```

## Synchronous report

```cpp
Logger::report(
    Logger::Types::Level::Error,
    "SaveSystem",
    "Could not open save file"
);
```

## Fatal popup report

```cpp
Logger::report(
    Logger::Types::Level::Fatal,
    "Startup",
    Logger::Types::ReportPopup::Fatal,
    "Critical startup failure"
);
```

Normal `Logger::fatal(...)` is only a fatal-severity normal log. It does not terminate and does not force a popup.

## Bounded flush

```cpp
const bool drained = Logger::flush(std::chrono::milliseconds{250});
```

Use bounded flushes in paths where waiting forever would be worse than reporting an incomplete drain.

## Stats

```cpp
const auto stats = Logger::getStats();
const auto memory = Logger::getMemoryStats();
Logger::resetStats();
```

Use stats in tests and diagnostics. Do not treat coverage or hook-enabled builds as performance baselines.
