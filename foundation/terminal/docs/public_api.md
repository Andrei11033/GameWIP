@page terminal_public_api Terminal public API

Terminal APIs live in `GameWIP::Terminal`. Option, capability, and result types live in `GameWIP::Terminal::Types`.

## API groups

| Group | Main APIs | Details |
| --- | --- | --- |
| Input | `readLine`, `readText`, `readBytes`, `Reader` | @ref terminal_read_write |
| Output | `writeText`, `writeLine`, `writeBytes`, `writeSegments`, `print`, `println`, `OutputBuffer`, `Writer` | @ref terminal_read_write and @ref terminal_segmented_writes |
| Styling | `TextStyle`, `Color`, `StyleMode`, `TextWriteOptions`, `LineWriteOptions` | @ref terminal_styling |
| Capabilities | `getInputCapabilities`, `getOutputCapabilities`, `getInputAvailability` | @ref terminal_capabilities_and_redirection |
| Input modes | `getInputMode`, `setInputMode`, `restoreDefaultInputMode`, `InputModeScope` | @ref terminal_input_modes |
| Controls | cursor, clear, scroll, alternate-screen, title, bell, flush APIs | @ref terminal_control_primitives |

## Error model

Terminal returns `IO::Types::Status` or result types that contain `status`. Expected unsupported, detached, timeout, and encoding cases are reported through those result shapes rather than exceptions.

Formatting overloads use `std::format` semantics before writing.
