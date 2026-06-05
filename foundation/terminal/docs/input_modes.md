@page terminal_input_modes Terminal input modes

Input mode query, set, restore, and scoped restoration are implemented for Windows real console stdin. Redirected or detached input streams report explicit unsupported or not-open statuses for mode operations.

## Presets

`InputModePreset::Default` requests the backend/default mode for the input stream.

`InputModePreset::InteractiveLine` requests normal interactive input: line-buffered, echoed, and platform control keys processed.

`InputModePreset::RawBytes` requests raw byte-oriented input where practical.

Backends may return `Unsupported` when a requested mode cannot be represented on the current stream.

## InputModeScope

`InputModeScope` restores the previous input mode for a temporary mode change.

Destructors must not throw. A destructor should attempt best-effort restoration when the scope is active, but callers that care about restoration failure must call `restore()` explicitly and inspect the returned `IO::Types::Status`.

`release()` disables automatic restoration. Use it only when ownership of restoration has intentionally moved somewhere else.

## Safety

Prefer `InputModeScope` for raw or non-echoing modes so ordinary exits restore interactive input.

Terminal implementation code must serialize input-mode changes with other Terminal input operations for the same stream.
