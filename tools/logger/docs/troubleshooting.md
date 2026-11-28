@page logger_troubleshooting Troubleshooting

Logger preserves effective output, health, counters, and the original failure
status so a caller can distinguish a fallback from a complete logging outage.
Inspect those values first, then use the matching case below.

## Init succeeded but File is unavailable

Inspect the initialization result's `value().requestedOutput`, `value().effectiveOutput`, and `value().outputSetupStatus`. Successful Console fallback
keeps `ok()` true while preserving the File/setup failure directly. If File
remains effective but setup reported a failure, inspect `getHealth()` for a
degraded setup such as timestamp conversion failure.

## Logger became degraded

Inspect `getHealth()`. A sink that fails at runtime is disabled for that initialization so Logger does not repeatedly hammer a broken channel.
Surviving sinks continue.

## A timed flush/report did not complete

Check `value().outcome`. `TimedOut` with a successful status means the deadline was respected before completion. A failed `status()` means a real
sink/platform operation failed. Native IO already started may outlast the deadline.

## Reports appear but normal logs do not

Reports bypass filters and queue pressure. Check `running()`, `getOutput()`, source/level filters, queue-drop counters, and `getHealth()`.

## UTF-8 text was rejected

Configuration/source names and synchronous report/debug boundaries validate UTF-8. Normal hot messages are required to be valid UTF-8 by contract.
Logger does not normalize, insert/remove BOMs, or silently repair malformed input.
