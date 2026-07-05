@page terminal_public_api Terminal public API

Include `terminal/terminal.h`. Installed-package consumers link `GameWIP::Terminal`; builds within the source tree link the short `Terminal` target. See @ref terminal_quick_start for complete CMake examples.

Passive options, capabilities, and result shapes live in `GameWIP::Terminal::Types`. Free functions provide direct stdin/stdout/stderr access through paired default-stream and explicit-stream overloads.

## Output selection

Use plain text writes for allocation-free unstyled output, formatted writes for `std::format` semantics, segmented writes for one logical record with mixed styles or bytes, and `OutputBuffer` when the caller intentionally wants delayed batching.

`getOutputCapabilities()` observes current support without changing terminal state. `prepareOutput()` enables platform features required by styles and controls; those operations also prepare lazily when needed.

Terminal serializes each operation per standard stream. Calls through this API are thread-safe at that boundary, but multi-call workflows are not transactions and Terminal cannot coordinate with unrelated output APIs.

Terminal is a shared library so every linked module coordinates through the same process-wide stream state. Installed-package consumers link `GameWIP::Terminal`; builds inside this source tree link the short `Terminal` target.

## State management

Input mode, alternate screen, and cursor visibility have RAII scopes for temporary state changes. Input-mode scopes restore the complete backend mode captured before setup. Cursor-hidden and alternate-screen scopes are nesting-safe per output stream. The scopes track setup and restoration status but do not create a global Terminal lifecycle.

Expected detached, unsupported, timeout, invalid-option, encoding, and formatted stream-write failures use IO status/result types. `OutputBuffer` formatting and an invalid `OutputBuffer` constructor argument retain normal C++ exception behavior because they operate on caller-owned in-memory state before a terminal write is requested.

The focused contracts are documented in @ref terminal_read_write, @ref terminal_styling, @ref terminal_segmented_writes, @ref terminal_capabilities_and_redirection, @ref terminal_input_modes, and @ref terminal_control_primitives.
