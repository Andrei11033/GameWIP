@page terminal Terminal

`GameWIP::Terminal` provides platform-neutral UTF-8 access to process standard input and output, terminal capabilities, styling, and low-level terminal controls.

Terminal is a shared runtime library. One process-wide instance owns standard-stream serialization, reusable output state, input modes, cursor scopes, alternate-screen nesting, and test hooks across the application and shared libraries such as Logger.

## User manual

- @subpage terminal_quick_start
- @subpage terminal_public_api
- @subpage terminal_read_write
- @subpage terminal_segmented_writes
- @subpage terminal_styling
- @subpage terminal_capabilities_and_redirection
- @subpage terminal_input_modes
- @subpage terminal_control_primitives
- @subpage terminal_unicode_io
- @subpage terminal_examples
- @subpage terminal_troubleshooting

## Developer validation

- @subpage terminal_testing

## Generated API reference

Use @ref GameWIP::Terminal for active operations and RAII scopes, and @ref GameWIP::Terminal::Types for streams, capabilities, styles, options, and results. These generated pages document every public type, enum value, field, function, overload, and constant from `terminal/terminal.h`; the manual pages above organize the behavior by workflow.
