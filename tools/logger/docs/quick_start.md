@page logger_quick_start Quick start

## Include

```cpp
#include "logger/logger.h"
```

Include the macro header only in translation units that use the optional global shortcuts:

```cpp
#include "logger/logger_macros.h"
```

## Installed CMake

Set `GAMEWIP_REQUIRED_VERSION` from the consuming project's dependency lock; see @ref project_library_compatibility.

```cmake
find_package(Logger ${GAMEWIP_REQUIRED_VERSION} EXACT CONFIG REQUIRED)
target_link_libraries(MyTarget PRIVATE GameWIP::Logger)
```

## Source-tree CMake

```cmake
target_link_libraries(MyTarget PRIVATE Logger)
```

## Minimal usage

```cpp
#include "logger/logger.h"

#include <array>

namespace Logger = GameWIP::Logger;

enum class LogSource : Logger::Types::SourceId
{
    Game = 1,
    Assets = 2,
};

int main()
{
    constexpr std::array sources{
        Logger::defineSource(LogSource::Game, "Game"),
        Logger::defineSource(LogSource::Assets, "Assets"),
    };

    Logger::Types::Config config = Logger::defaultConfig();
    config.output = Logger::Types::Output::Console;
    config.sources = sources;

    const Logger::Types::Result initResult = Logger::init(config);
    if (!Logger::isRunning())
    {
        return 1;
    }

    // A non-Success init result can describe a sanitized option or sink fallback.
    // Inspect the effective state before deciding whether startup must fail.
    static_cast<void>(initResult);
    static_cast<void>(Logger::getOutput());
    static_cast<void>(Logger::getQueueLimits());

    Logger::info(LogSource::Game, "Application started");
    Logger::warn(LogSource::Assets, "Optional asset '{}' is unavailable", "defaults.json");

    Logger::shutdown();
    return 0;
}
```

The source-registration array must remain alive until `init()` returns because `Config::sources` is a span. Logger copies the source IDs and names during initialization.

## Failure handling

`init()` returns the first setup or validation result it observes. Some non-success results are recoverable: Logger may sanitize queue/message limits or fall back from file output to console and still start. Determine the effective state with `isRunning()`, `getOutput()`, `getQueueLimits()`, `getLastResult()`, and `getLastPlatformError()`.

Normal log calls return `void`. A filtered call, a call made while Logger is disabled, a queue drop, or an internal formatting/allocation failure is therefore observed through configuration, statistics, and diagnostic state rather than a per-call result.

Use synchronous reports for important diagnostics that must bypass filters and queue pressure:

```cpp
Logger::reportError("Startup", "Could not load required configuration");
```

A timed report uses one best-effort deadline across its report/flush attempt and the drain of older queued records:

```cpp
const bool drained = Logger::reportError(
    "Startup",
    Logger::flushTimeout(std::chrono::milliseconds{250}),
    "Could not load required configuration");
```

`drained == false` does not mean the report line was skipped; it means an observable file flush failed, a Logger-owned lock wait expired, or the queue did not drain. Native sink I/O already in progress cannot be cancelled and may overrun the deadline.

## Where to go next

- @ref logger_public_api inventories the complete supported surface.
- @ref logger_configuration explains every configuration field and initialization result.
- @ref logger_messages_sources explains sources, formats, copying, and truncation.
- @ref logger_lifecycle defines startup, flush, shutdown, and state-query behavior.
- @ref logger_output explains console, file, debugger, and popup channels.
- @ref logger_threading_performance explains queue pressure and concurrency.
- @ref logger_reports explains synchronous reporting and termination.
- @ref logger_examples provides complete usage recipes.
