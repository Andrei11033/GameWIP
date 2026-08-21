@page logger_examples Examples

These examples build on the lifecycle from @ref logger_quick_start and focus on
configuration, structured sources, reports, and health inspection.

## File logger with fallback visibility

```cpp
GameWIP::Logger::Types::Config config;
config.output = GameWIP::Logger::Types::OutputMode::File;
config.logDirectory = "logs";

const auto result = GameWIP::Logger::init(config);
if (!result.status.ok())
    return;

if (!result.outputSetupStatus.ok())
{
    // Logger is usable through result.effectiveOutput, but requested output degraded.
}
```

## Runtime filters

```cpp
const auto status = GameWIP::Logger::setLevelFilter(GameWIP::Logger::Types::Level::Debug, false);
if (!status.ok())
    return;
```

## Emergency report

```cpp
using namespace std::chrono_literals;
const auto report = GameWIP::Logger::reportFatal("Renderer", 250ms, "device lost: {}", code);
if (report.delivery != GameWIP::Logger::Types::Report::Delivery::Complete)
{
    // At least one enabled emergency channel did not receive the report.
}
```

## Reset runtime filters

```cpp
GameWIP::Logger::resetSourceFilters();
GameWIP::Logger::resetLevelFilters();
```

Reset operations restore the affected runtime filters to their default enabled state.
