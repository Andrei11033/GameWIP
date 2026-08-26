@page logger_configuration Configuration

`Logger::Types::Config` is copied by `init()`.

## Output

`output` defaults to `OutputMode::Both`. `OutputMode::None` is a successful disabled configuration. File-only startup may fall back to Console when
`fallbackToConsoleOnFileFailure` is true; `OutputMode::Both` keeps Console when File setup fails.

`logDirectory` and registered source names are UTF-8 and are validated at initialization. Invalid UTF-8 is rejected rather than repaired or
normalized.

## Queue and formatting

- `maxQueueSize == 0` is adjusted to the small fallback queue.
- `maxMessageLength == 0` is adjusted to the fallback message limit.
- invalid/non-finite hard-queue multipliers are adjusted to `1.0`.
- nonzero worker batch values are clamped to the hard queue limit.
- inline capacity is clamped to the message limit.
- a first queue allocation failure retries with the small fallback queue.
- `workerBatchSize == 0` means use the default and is not itself an adjustment.

These recoveries are successful initialization adjustments reported in `Types::Init::Result::adjustments`; they are not IO failures.

Undefined `OutputMode`, `Level`, or `FormatPolicy` enum values and invalid initial filters are rejected with `InvalidArgument`. Invalid `FormatPolicy`
is not silently replaced.

## `Types::Init::Result`

`status` answers whether initialization itself succeeded. `outcome` tells whether the resulting Logger is Started or Disabled. `requestedOutput` and
`effectiveOutput` expose fallback directly. `outputSetupStatus` preserves a failed/degraded requested output setup even when overall init succeeds via
fallback.

Use `getQueueLimits()` for authoritative effective queue/message values.
