@page logger_lifecycle Logger lifecycle

Logger has one process-wide runtime instance. It is initialized, used from producer threads, flushed when needed, and shut down before exit.

## Startup functions

| API | Use when |
| --- | --- |
| `Logger::defaultConfig()` | You want the normal starting config and will customize fields. |
| `Logger::lowMemoryConfig()` | You want smaller queue/message memory defaults. |
| `Logger::throughputConfig()` | You want larger queue/batch defaults for heavier logging. |
| `Logger::init(config)` | You have a complete custom config. |
| `Logger::initDefault()` | You want the default config without editing it. |
| `Logger::initConsole(level)` | You want a quick console logger. |
| `Logger::initFile(directory, level)` | You want a quick file logger. |

Example:

```cpp
auto config = GameWIP::Logger::throughputConfig();
config.minLevel = GameWIP::Logger::Types::Level::Debug;
GameWIP::Logger::init(config);
```

## Runtime state

```cpp
if (GameWIP::Logger::isRunning())
{
    const auto output = GameWIP::Logger::getOutput();
    const auto path = GameWIP::Logger::getLogFilePath();
}
```

Useful inspection APIs:

- `getMinLevel()`
- `getOutput()`
- `getLogFilePath()`
- `getQueueLimits()`
- `getLastResult()`
- `getLastPlatformError()`

## Flush and shutdown

`flush()` waits until accepted queued records have been drained and sinks are flushed.

```cpp
GameWIP::Logger::flush();
```

Use the timeout overload when a shutdown path should not wait forever:

```cpp
const bool drained = GameWIP::Logger::flush(std::chrono::milliseconds{250});
```

`shutdown()` stops normal acceptance, drains accepted work, flushes sinks, and tears down the worker.

```cpp
GameWIP::Logger::shutdown();
```

## Important lifecycle rules

- `init()` copies config state it needs; mutating the config object after `init()` does not reconfigure the logger.
- Calling `init()` while already running returns `AlreadyRunning`.
- Calling `shutdown()` repeatedly is safe as cleanup, but only a running logger can drain accepted work.
- Producer threads should be stopped before final shutdown when possible.
- Calls before init or after shutdown should not be treated as guaranteed persistent logging.
- `flush(timeout)` can return false when producers keep adding work or sinks cannot finish before the bounded wait.
- Repeated init/shutdown cycles should reset runtime state, filters, queue storage, and test-hook state according to the active build.

## Related pages

- @ref logger_configuration
- @ref logger_threading_performance
- @ref logger_troubleshooting
