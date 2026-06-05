@page terminal_public_api Terminal public API

Include `terminal/terminal.h` and link `Terminal`.

Passive options, capabilities, and result shapes live in `GameWIP::Terminal::Types`. Free functions provide direct stdin/stdout/stderr access; `Reader` and `Writer` store default streams for repeated operations.

## Output selection

Use plain text writes for allocation-free unstyled output, formatted writes for `std::format` semantics, segmented writes for one logical record with mixed styles or bytes, and `OutputBuffer` when the caller intentionally wants delayed batching.

`getOutputCapabilities()` observes current support without changing terminal state. `prepareOutput()` enables platform features required by styles and controls; those operations also prepare lazily when needed.

Terminal serializes each operation per output stream. It cannot coordinate with unrelated output APIs.

## State management

Input mode, alternate screen, and cursor visibility have RAII scopes for temporary state changes. The scopes track setup and restoration status but do not create a global Terminal lifecycle.

Expected detached, unsupported, timeout, and encoding failures use IO status/result types. Formatting errors retain normal `std::format` exception behavior.

The focused contracts are documented in @ref terminal_read_write, @ref terminal_styling, @ref terminal_segmented_writes, @ref terminal_capabilities_and_redirection, @ref terminal_input_modes, and @ref terminal_control_primitives.
