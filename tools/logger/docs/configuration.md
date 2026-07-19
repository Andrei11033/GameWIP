@page logger_configuration Configuration

`Logger::Types::Config` is consumed by `init()`. Logger copies the strings, source definitions, and filter state it retains before `init()` returns; later edits to the caller's `Config` and backing arrays do not reconfigure the runtime.

## Presets

| Factory | Intended tradeoff | Changed queue/message fields |
| --- | --- | --- |
| `defaultConfig()` | General-purpose baseline. | Soft queue 1024, hard multiplier 1.25, message limit 4096, inline capacity 256, batch 256. |
| `lowMemoryConfig()` | Lower retained storage and post-spike retention. | Soft queue 256, hard multiplier 1.0, message limit 1024, inline capacity 128, batch 64, release message/storage enabled. |
| `throughputConfig()` | Larger bursts and less reallocation across reinitialization. | Soft queue 4096, hard multiplier 1.25, message limit 4096, inline capacity 256, batch 512, release message/storage disabled. |

All other fields retain their `Config` defaults unless changed by the caller.

## Output and path fields

| Field | Default | Contract |
| --- | --- | --- |
| `output` | `Both` | Selects normal console/file sinks. `None` creates a successfully configured disabled runtime with no worker. |
| `logDirectory` | `"logs"` | UTF-8 directory for file output. Empty selects the same default. Relative paths use the process working directory at initialization. |
| `fallbackToConsoleOnFileFailure` | `true` | Allows a file-only request to continue as console output when file setup fails. `Both` already retains console output. |
| `enableConsoleColor` | `true` | Requests severity styling through Terminal; redirected streams receive plain text. |
| `enableDebugOutput` | `true` | Enables explicit debugger output and report mirroring. It is independent of normal sink selection. |
| `enableFatalPopup` | `true` | Enables popup attempts only for report paths that request `ReportPopup::Fatal`. |
| `flushFileEveryBatch` | `false` | Flushes file data after each worker batch. |
| `flushConsoleEveryWrite` | `false` | Flushes the selected standard stream after each console record. |

@ref logger_output owns sink routing and line-format details.

## Severity and filters

| Field | Default | Contract |
| --- | --- | --- |
| `minLevel` | `Info` | Permanent startup floor for the active initialization. Runtime filters cannot re-enable lower levels. |
| `sources` | empty | Registered ID/name table copied during initialization. IDs and names must be non-duplicate; names must be non-empty. |
| `sourceFilters` | empty | Initial enabled states for registered source IDs. Unknown IDs make initialization fail with `InvalidSourceFilter`. |
| `levelFilters` | empty | Initial enabled states for exact severities. Invalid enum values make initialization fail with `InvalidLevelFilter`. |

String sources are controlled only by `minLevel` and level filters. Source filters apply to registered `SourceId` and enum-source calls.

## Queue and formatting fields

| Field | Default | Contract |
| --- | --- | --- |
| `maxQueueSize` | 1024 | Soft depth. `Trace` through `Warn` may drop at or above this depth. |
| `hardQueueMultiplier` | 1.25 | Hard depth is `ceil(maxQueueSize * multiplier)`, never below the soft depth. Every severity may drop at the hard depth. |
| `maxMessageLength` | 4096 | Maximum retained message bytes, including the truncation suffix. It is a byte limit, not a Unicode code-point limit. |
| `formatPolicy` | `StrictBounded` | Chooses bounded formatting or reusable full-format scratch followed by truncation. Invalid enum values fall back to `StrictBounded`. |
| `inlineMessageCapacity` | 256 | Per-queue-slot preallocated message bytes. Zero disables the arena; values above the message limit are clamped. |
| `workerBatchSize` | 256 | Maximum worker drain batch. Zero selects the library default; the effective value is clamped to `[1, hardQueueSize]`. |
| `releaseMessageMemoryAfterWrite` | `false` | Releases oversized heap fallback capacity after entries are cleared instead of retaining it for reuse. |
| `releaseStorageOnShutdown` | `true` | Releases queue, batch, arena, and source-registry storage during shutdown. When false, storage may be retained for a later initialization; callers must not depend on an exact retained capacity. |

### Sanitization

`maxQueueSize == 0` is replaced with 4. `maxMessageLength == 0` is replaced with 512. A non-finite or less-than-one hard multiplier becomes 1.0. Queue-allocation failure triggers a second attempt with a four-entry queue. These recoveries preserve the first `InvalidQueueSize` or `InvalidMessageLength` result while allowing Logger to continue when setup succeeds.

Use `getQueueLimits()` after initialization to inspect authoritative effective values.

The returned `QueueLimits` fields are:

| Field | Effective value |
| --- | --- |
| `softQueueSize` | Sanitized soft queue depth. |
| `hardQueueSize` | Authoritative rounded/fallback hard queue depth. |
| `hardQueueMultiplier` | Sanitized multiplier requested for hard-limit derivation. |
| `maxMessageLength` | Effective retained-message byte limit. |
| `inlineMessageCapacity` | Effective per-slot arena capacity. |
| `workerBatchSize` | Effective worker drain batch. |

## Interpreting `init()` results

A non-`Success` result does not always mean Logger is unavailable.

| Result family | Typical effective state |
| --- | --- |
| `Success` | Requested configuration started, or `Output::None` was configured successfully. |
| `AlreadyRunning` | Existing runtime remains unchanged. |
| Invalid output, severity, source definition, or initial filter | Initialization is rejected; no new worker is started. |
| `InvalidQueueSize` or `InvalidMessageLength` | Logger may be running with sanitized values. Inspect `isRunning()` and `getQueueLimits()`. |
| File path/open/setup failure | Logger may be running with console fallback, may retain console from `Both`, or may have no active normal sink. Inspect `getOutput()` and `getLastPlatformError()`. |
| `ThreadStartFailed` | No running asynchronous logger. |

`getLastResult()` is process-wide mutable diagnostic state. Later operations can replace it; do not treat it as an immutable result object for an earlier call. See @ref logger_public_api for every `Result` enumerator.

## Compile-time macro controls

`LOGGER_TRACE` and `LOGGER_DEBUG` are included in non-`NDEBUG` translation units. In release-style translation units they compile out unless `LOGGER_ENABLE_TRACE_LOGS` or `LOGGER_ENABLE_DEBUG_LOGS` is defined, respectively.

These definitions are translation-unit compile controls, not runtime configuration. Apply them consistently at target level. See @ref logger_macros.

## Related pages

- @ref logger_lifecycle
- @ref logger_messages_sources
- @ref logger_threading_performance
- @ref logger_output
