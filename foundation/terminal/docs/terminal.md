@page terminal Terminal

`GameWIP::Terminal` provides platform-neutral UTF-8 access to process standard input and output, capability discovery, styling, input-mode control, and low-level terminal controls.

Terminal is a shared runtime library. One process-wide implementation coordinates standard-stream access across the application and shared libraries such as Logger; callers do not create or initialize a Terminal object.

## Consumer manual

- @subpage terminal_quick_start
- @subpage terminal_public_api
- @subpage terminal_abi
- @subpage terminal_read_write
- @subpage terminal_segmented_writes
- @subpage terminal_styling
- @subpage terminal_capabilities_and_redirection
- @subpage terminal_input_modes
- @subpage terminal_control_primitives
- @subpage terminal_unicode_io
- @subpage terminal_examples
- @subpage terminal_troubleshooting

## Maintainer validation

- @subpage terminal_testing
- @subpage terminal_test_hooks

## Generated API reference

Use @ref GameWIP::Terminal for operations, factories, formatted output, buffers, and RAII scopes. Use @ref GameWIP::Terminal::Types for streams, capabilities, styles, options, segments, and result types.

The generated reference owns exact declarations and overload signatures from `terminal/terminal.h`. The manual explains selection rules, endpoint-dependent behavior, ownership, failure semantics, concurrency, packaging, and caveats.

## Key behavior

Public text is UTF-8. Real-console text is converted through the native Unicode console API, while redirected text remains UTF-8 bytes. Raw byte operations are available for redirected endpoints and do not perform text validation.

Terminal serializes each operation per standard stream. stdin has one process-wide lock; stdout and stderr have independent process-wide locks. A single Terminal call is a serialization unit, but a sequence of calls is not a transaction and Terminal cannot coordinate with direct C, C++, native-handle, or third-party stream access.

Capabilities depend on the current endpoint. Real terminals, redirected streams, detached streams, and other handles can support different operations. Capability queries are snapshots; the status returned by the requested operation remains authoritative.

## Dependency boundary

The public C++ header is `terminal/terminal.h`. Installed consumers link `GameWIP::Terminal`; source-tree consumers may link `Terminal`.

Terminal publicly depends on IO for statuses, flush modes, and byte-write results. Logger uses Terminal for normal console output, but Terminal does not depend on Logger. Terminal owns standard-stream coordination, UTF-8/native conversion, capability discovery, styling, and primitive controls; higher-level prompts, menus, layout, logging policy, and game runtime behavior belong elsewhere.
