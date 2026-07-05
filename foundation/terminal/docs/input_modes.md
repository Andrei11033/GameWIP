@page terminal_input_modes Terminal input modes

Input mode query, set, restore, and scoped restoration are implemented for Windows real console stdin. Redirected or detached input streams report explicit unsupported or not-open statuses for mode operations.

## Presets

`InputModePreset::InteractiveLine` requests normal interactive input: line-buffered, echoed, and platform control keys processed.

`InputModePreset::RawBytes` requests raw byte-oriented input where practical.

Backends may return `Unsupported` when a requested mode cannot be represented on the current stream.

On Win32, echo requires line-buffered input. A mode with `echoInput = true` and `lineBuffered = false` returns `InvalidArgument` without changing the console mode.

On Windows real-console input, finite and non-blocking reads are unsupported even in raw mode. Blocking reads preserve native cooked input, echo, control-key processing, and line editing.

Use `restoreDefaultInputMode()` to restore the backend mode captured for the stream. A preset does not represent backend-specific default state.

## InputModeScope

`InputModeScope` captures and restores the complete previous backend mode for a temporary mode change. On Win32 this includes native console flags that are not represented by the three portable `InputMode` booleans.

Capture and mode application occur while holding the same Terminal input lock, so another Terminal input operation cannot interleave between them. Restore is serialized through that lock as well.

Destructors must not throw. A destructor should attempt best-effort restoration when the scope is active, but callers that care about restoration failure must call `restore()` explicitly and inspect the returned `IO::Types::Status`.

`release()` disables automatic restoration. Use it only when ownership of restoration has intentionally moved somewhere else.

## Safety

Prefer `InputModeScope` for raw or non-echoing modes so ordinary exits restore interactive input.

Changing or restoring input mode does not discard bytes or an incomplete Unicode sequence already buffered by Terminal. It does not promise to preserve unread input that an external API removes directly from the native stream.

Terminal serializes input-mode changes with other Terminal input operations for the same stream. Restore nested scopes in reverse acquisition order.

If the process replaces stdin, the next mode operation uses the new handle's default mode instead of restoring state from the previous handle.
