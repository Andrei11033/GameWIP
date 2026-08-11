@page terminal_control_primitives Control primitives

Terminal exposes low-level cursor, screen, title, bell, and alternate-screen operations. They are building blocks for higher-level tools, not a widget, layout, prompt, menu, progress, mouse, keyboard-event, or terminal-session framework.

Controls require a supported terminal endpoint. Redirected, detached, and unclassified endpoints commonly return `Unsupported` or `NotOpen`.

## Cursor movement and position

`moveCursor()` accepts `Up`, `Down`, `Left`, or `Right`. `setCursorPosition()` uses zero-based public coordinates even when the backend protocol is one-based.

A zero movement or scroll amount emits no control sequence, but the operation can still honor a requested flush.

`getCursorPosition()` can use an output endpoint for the query and an input endpoint for a protocol response. The current Win32 real-console backend answers directly without consuming stdin; its timeout therefore does not cause a wait. Its public coordinates are relative to the current visible `srWindow`, matching `getTerminalSize()` viewport dimensions. Managed line echo uses a private stable screen-buffer coordinate instead because a viewport scroll must not invalidate an active wrapped line.

`saveCursorPosition()` and `restoreCursorPosition()` expose backend save/restore state. They are not specified as a stack: repeated saves may replace the previously saved position. Do not assume arbitrary nesting.

## Clearing and scrolling

`clear()` targets the whole visible screen, regions before/after the cursor, the screen plus scrollback where supported, or the current line and its regions. `scroll()` moves terminal content up or down.

The active backend validates parameters. The current Win32 VT path limits movement and scroll values to 32767 and zero-based absolute coordinates to 32766.

## Temporary cursor visibility

`scopedCursorHidden()` returns a `CursorHiddenScope`. Nested active scopes on the same stream emit one hide at the outer transition and one show after the final scope restores.

The factory is `noexcept`. Inspect both `status()` and `active()`: failure before the hide sequence is emitted produces an inactive scope, while a requested flush failure after emission reports failure but leaves the scope active with restoration responsibility. `restore()` is idempotent for an inactive scope; failed explicit restoration remains active for retry. The destructor makes one best-effort non-throwing restore and releases its nesting bookkeeping if restoration fails because no scope remains through which to retry.

## Alternate screen

`scopedAlternateScreen()` follows the same status, nesting, retry, and destructor rules. Manual enter/leave functions are available when state must outlive one lexical scope.

Do not mix manual visibility or alternate-screen transitions with active scopes for the same stream. Restore scopes in reverse acquisition order.

## Scope move assignment

`CursorHiddenScope` and `AlternateScreenScope` are movable and non-copyable. Move assignment first restores/leaves state currently owned by the destination. If that operation fails, the destination remains active and the source is not consumed.

## Title and bell

`setTitle()` accepts UTF-8. The current Win32 backend limits the title to 254 UTF-8 bytes and replaces all C0 control bytes plus DEL with spaces before constructing the control sequence.

`ringBell()` emits the terminal bell control. It cannot guarantee an audible sound; user and terminal settings decide the final effect.

## Flush and failure behavior

Controls use `ControlOptions::flushMode`. Invalid values are rejected before normal emission. A requested flush reaches the operating system only where the endpoint supports a meaningful flush.

A failed control write can already have emitted a prefix. Status reports completion, not rollback. For the two state-owning scope factories, successful sequence emission is tracked separately from the following flush so a flush failure cannot lose the required inverse transition. When an inverse sequence is emitted but its flush fails, retry flushes without emitting that inverse sequence again.

See @ref terminal_capabilities_and_redirection and @ref terminal_styling.
