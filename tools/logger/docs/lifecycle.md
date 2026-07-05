@page logger_lifecycle Logger lifecycle

Logger has one process-wide runtime instance. It is initialized, used from producer threads, flushed when needed, and shut down before exit.

## Startup functions

| API | Use when |
| --- | --- |
| `Logger::defaultConfig()` | Start from the normal configuration and customize fields. |
| `Logger::lowMemoryConfig()` | Use smaller queue and message-memory defaults. |
| `Logger::throughputConfig()` | Use larger queue and batch defaults for heavier logging. |
| `Logger::init(config)` | Initialize from a complete custom configuration. |
| `Logger::initDefault()` | Initialize with the default configuration without editing it. |
| `Logger::initConsole(level)` | Initialize a console-only logger. |
| `Logger::initFile(directory, level)` | Initialize a file-only logger. |

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

- `init()` copies the configuration state it needs; mutating the configuration object after `init()` does not reconfigure Logger.
- Calling `init()` while already running returns `AlreadyRunning`.
- Calling `shutdown()` repeatedly is safe as cleanup, but only a running logger can drain accepted work.
- Producer threads should be stopped before final shutdown when possible.
- Calls before init or after shutdown should not be treated as guaranteed persistent logging.
- `flush(timeout)` can return false when producers keep adding work or sinks cannot finish before the bounded wait.
- Repeated init/shutdown cycles reset runtime state, filters, and queue storage according to the active configuration.

## Related pages

- @ref logger_configuration
- @ref logger_threading_performance
- @ref logger_troubleshooting
