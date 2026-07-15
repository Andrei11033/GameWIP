@page terminal_input_modes Terminal input modes and scoped restoration

Input-mode operations apply to real terminal stdin when the backend can represent the requested portable mode. Redirected, detached, or unsupported endpoints return explicit statuses.

## Portable modes

`InputMode` contains `lineBuffered`, `echoInput`, and `processControlKeys`. `makeInputMode()` creates values for:

- `InteractiveLine`: line buffering, echo, and platform control-key processing enabled;
- `RawBytes`: raw byte-oriented input where practical.

A preset is a portable request, not the complete backend default. Use `restoreDefaultInputMode()` to restore the mode captured for the current native stdin handle.

On Win32, echo requires line-buffered input. `echoInput == true` with `lineBuffered == false` returns `InvalidArgument` without changing native mode.

Finite and non-blocking reads remain unsupported for the current Windows real-console path even in raw mode.

## InputModeScope

`scopedInputMode()` captures the complete previous backend state and applies the requested mode while holding Terminal's input lock. The returned `InputModeScope` stores both the portable and native state required for exact restoration.

The factory is `noexcept`. Setup failure produces an inactive scope whose `status()` reports the failure:

```cpp
const auto raw = GameWIP::Terminal::makeInputMode(
    GameWIP::Terminal::Types::InputModePreset::RawBytes);
auto scope = GameWIP::Terminal::scopedInputMode(raw);
if (!scope.status().ok())
{
    // The requested mode was not activated.
}
```

`restore()` is idempotent for an inactive scope and returns its last tracked status. A failed explicit restoration leaves the scope active so the caller can retry. `release()` abandons restoration responsibility without changing the current mode.

The destructor never throws and makes a best-effort restore. Call `restore()` explicitly when restoration failure must be observed.

## Move behavior

Scopes are movable and non-copyable. Move construction transfers responsibility. Move assignment first tries to restore the destination's currently owned mode. If that restoration fails, the destination remains active and the source is not consumed.

Restore nested scopes in reverse acquisition order.

## Buffered input and handle replacement

Changing or restoring input mode does not discard bytes or an incomplete Unicode sequence already buffered inside Terminal. It cannot preserve unread input removed by an external API.

If the process replaces stdin, the next mode operation recognizes the new handle and uses that handle's captured default rather than restoring state belonging to the previous handle.

On Win32, Terminal retains a duplicated identity handle for stdin. A replacement, detachment, or reused numeric handle clears pending bytes, incomplete surrogate state, availability scratch, and captured default-mode state before the new endpoint is read.

See @ref terminal_read_write and @ref terminal_capabilities_and_redirection.
