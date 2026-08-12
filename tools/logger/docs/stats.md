@page logger_stats Statistics and health

`getStats()` remains a relaxed snapshot of resettable counters. It is intended for diagnostics/tests rather than transactional state. Queue/drop/allocation/format/truncation counters keep their existing meanings, and `resetStats()` does not alter health.

`getHealth()` is separate and coherent. `HealthSnapshot` contains current `Healthy`, `Degraded`, or `Disabled` state, effective normal output, compact failure source, portable IO error, native code, and the failure count for the current initialization epoch.

A failed normal sink is disabled for the rest of that Logger initialization. With `Both`, loss of File leaves Console and health becomes Degraded; loss of the final normal sink leaves `OutputMode::None` and health Disabled. Debug output and fatal-popup failures disable only their own emergency channel. There is no automatic sink resurrection; a new `shutdown()`/`init()` cycle is the retry boundary.

A real new init clears the health failure epoch. An `AlreadyOpen` init attempt does not. Shutdown publishes Disabled while retaining the last failure metadata for postmortem inspection until the next init.

`getLastResult()` and `getLastPlatformError()` no longer exist; synchronous failures belong to the result returned by the operation that produced them.
