@page logger_troubleshooting Troubleshooting

## Init succeeded but File is unavailable

Inspect `Types::Init::Result::requestedOutput`, `effectiveOutput`, and `outputSetupStatus`. Successful Console fallback keeps overall `status` successful while preserving the File/setup failure directly.

## Logger became degraded

Inspect `getHealth()`. A sink that fails at runtime is disabled for that initialization so Logger does not repeatedly hammer a broken channel. Surviving sinks continue.

## A timed flush/report did not complete

Check the result's `outcome`. `TimedOut` with successful IO status means the deadline was respected before completion. A failed status means a real sink/platform operation failed. Native IO already started may outlast the deadline.

## Reports appear but normal logs do not

Reports bypass filters and queue pressure. Check `isRunning()`, `getOutput()`, source/level filters, queue-drop counters, and `getHealth()`.

## UTF-8 text was rejected

Configuration/source names and synchronous report/debug boundaries validate UTF-8. Normal hot messages are required to be valid UTF-8 by contract. Logger does not normalize, insert/remove BOMs, or silently repair malformed input.
