@page logger_lifecycle Lifecycle

Logger lifecycle operations are serialized.

## Initialization

A successful enabled init returns `status.ok() == true` and `Types::Init::Outcome::Started`. `OutputMode::None` returns success plus `Disabled`. Recoverable numeric/storage adjustments remain successful and are described by the adjustment bitmask.

If File setup fails and Console remains available, overall initialization succeeds, `effectiveOutput` reflects Console, and `outputSetupStatus` contains the original File/setup failure. If no normal sink remains, init fails and returns `Disabled`.

Calling init while Logger is active returns `AlreadyOpen`, preserves the existing runtime and health, and does not reconfigure it. Reconfiguration is explicit: `shutdown()` then `init()`.

## Flush

`flush()` drains accepted asynchronous work and flushes active sinks. The optional duration controls one absolute Logger-owned deadline: `nullopt` waits indefinitely, zero polls, positive values bound waits, and negative values are invalid.

A respected deadline produces `status.ok() == true` plus `FlushOutcome::TimedOut`. Real write/flush failures are represented by failed IO status. Native IO already entered is synchronous and may complete after the deadline.

## Shutdown

`shutdown()` is repeat-safe and safe before init. It disables producer acceptance, joins/drains the worker, flushes sinks, closes File, and releases configured storage. Cleanup continues after failures and returns the first real IO failure. The final runtime state is Disabled.
