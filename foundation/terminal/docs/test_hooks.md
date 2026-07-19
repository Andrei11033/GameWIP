@page terminal_test_hooks Source-tree test hooks

@warning These hooks are source-tree maintainer interfaces. They are not installed, not consumer API, and not covered by package compatibility guarantees.

## Availability

The GameWIP validation preset enables `TERMINAL_ENABLE_TEST_HOOKS`. A focused build may set it explicitly:

```cmake
set(TERMINAL_ENABLE_TEST_HOOKS ON CACHE BOOL "" FORCE)
```

Approved build-tree consumers receive `INTERNAL_TERMINAL_TEST_HOOKS=1`. Production code must not define that macro manually or depend on hook symbols.

## Include and link

```cpp
#include "terminal/internal/terminal_test_hooks.h"
```

Link the source-tree `Terminal` target. The header and hook exports are intentionally absent from installed packages.

The public hook namespace is `GameWIP::Terminal::TestHooks`.

## Reset rule

Call `TestHooks::reset()` before and after each scenario. It clears capability overrides, prepared state, input bytes, mode overrides, output capture, counters, size/position overrides, and every one-shot forced failure.

Tests sharing a process must not assume hook state is isolated automatically.

## Capability overrides

- `setInputCapabilitiesOverride()` and `clearInputCapabilitiesOverride()` control one input stream's observed capabilities.
- `setOutputCapabilitiesOverride()` and `clearOutputCapabilitiesOverride()` control active output capabilities.
- `setPreparedOutputCapabilitiesOverride()` controls capabilities returned after a preparation attempt. Clearing the normal output override or calling `reset()` removes the prepared override.

Overrides persist until cleared or reset.

## In-memory input and modes

- `setInputBytes()` replaces the deterministic input bytes and selects EOF-versus-no-data behavior when the buffer becomes empty.
- `appendInputBytes()` adds bytes to the deterministic input stream.
- `clearInputBytes()` disables the in-memory input path.
- `setPendingHighSurrogate()` and `hasPendingHighSurrogate()` expose the Win32 converter's endpoint-owned surrogate state for the stdin-replacement regression.
- `setInputModeOverride()` provides deterministic current/default mode state.
- `clearInputModeOverride()` restores normal backend behavior.

The byte strings may intentionally contain invalid or incomplete UTF-8 to validate text-read failures.

## Output capture and counters

- `setOutputCapture()` enables or disables capture for stdout or stderr.
- `capturedOutput()` returns captured bytes.
- `capturedOutputText()` returns the captured bytes as a string for text-oriented assertions.
- `clearCapturedOutput()` clears captured data without disabling capture.
- `outputPreparationCallCount()` reports preparation attempts.
- `textWriteCallCount()` reports backend text-write calls.

Returned vectors and strings are snapshots owned by the caller.

## Exception behavior

Hook functions marked `noexcept` do not propagate failures. Other hook functions retain normal standard-library exception behavior: replacing or appending input can throw while allocating owned string storage, and `capturedOutput()` or `capturedOutputText()` can throw while allocating the returned snapshot. These source-tree hooks do not provide a status-based allocation-failure channel; reset the scenario before reuse after an unexpected exception.

## Geometry overrides

- `setTerminalSizeOverride()` / `clearTerminalSizeOverride()` control size queries.
- `setCursorPositionOverride()` / `clearCursorPositionOverride()` control cursor-position queries.

## One-shot failures

Each function arms one failure consumed atomically by the next matching operation:

- `forceNextInputCapabilityFailure()`;
- `forceNextOutputCapabilityFailure()`;
- `forceNextOutputPreparationFailure()`;
- `forceNextInputAvailabilityFailure()`;
- `forceNextInputModeFailure()`;
- `forceNextReadFailure()`;
- `forceNextTerminalSizeFailure()`;
- `forceNextCursorPositionFailure()`;
- `forceNextTextWriteFailure()`;
- `forceNextByteWriteFailure()`;
- `forceNextFlushFailure()`.

The optional argument selects the portable IO error code. One-shot failures are intended for one deterministic assertion; arm them immediately before the target operation.

## Example

```cpp
#include "terminal/internal/terminal_test_hooks.h"

#include <string>

int main()
{
    namespace Terminal = GameWIP::Terminal;
    namespace Hooks = GameWIP::Terminal::TestHooks;

    Hooks::reset();
    Hooks::setOutputCapture(Terminal::Types::OutputStream::Stdout, true);

    const auto status = Terminal::writeLine("captured");
    const std::string output = Hooks::capturedOutputText(
        Terminal::Types::OutputStream::Stdout);

    Hooks::reset();
    return status.ok() && !output.empty() ? 0 : 1;
}
```

## Related pages

- @ref terminal_testing
- @ref project_testing
- @ref project_extending
