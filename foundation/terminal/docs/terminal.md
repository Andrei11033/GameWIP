@page terminal Terminal

`GameWIP::Terminal` provides platform-neutral UTF-8 and byte standard-stream I/O, persistent managed input sessions, structured event contracts, capability discovery, styling, and low-level terminal controls.

Terminal is a shared runtime library. One process-wide implementation coordinates standard-stream access across the application and shared libraries such as Logger. Callers may use direct one-operation functions or create a move-only `Session` when interactive input needs persistent ownership and native state.

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

Use @ref GameWIP::Terminal for direct operations, `Session`, factories, formatted output, buffers, and output-state RAII scopes. Use @ref GameWIP::Terminal::Types for streams, session policies, events, capabilities, styles, options, segments, and result types.

The generated reference owns exact declarations and overload signatures from the `terminal/terminal.h` umbrella and focused `terminal/style.h` styling header. The manual explains selection rules, endpoint-dependent behavior, ownership, failure semantics, concurrency, packaging, and caveats.

## Key behavior

Public Text is complete valid UTF-8 independent of endpoint. Real-console output is converted through the native Unicode console API, while redirected text preserves the validated UTF-8 bytes unchanged. Native Win32 console input is decoded from structured `INPUT_RECORD` data into portable logical events; interactive Stream reads and Unicode line editing are built on that same immediate event engine. Raw byte operations remain arbitrary-byte oriented.

Terminal has one process-wide managed stdin ownership domain plus backend input serialization. A persistent `Session` owns stdin until `close()`, binds one primary output, and can track reversible output state it explicitly changes. Each direct read acquires temporary ownership, performs the operation, restores exact native state, and releases ownership. Global and Session output share the same independent stdout/stderr serialization, so output can continue while a Session read blocks. Terminal cannot coordinate with direct C, C++, native-handle, or third-party stream access.

Capabilities depend on the current endpoint. Real terminals, redirected streams, detached streams, and other handles can support different operations. Capability queries are snapshots; the status returned by the requested operation remains authoritative.

## Dependency boundary

The normal public umbrella is `terminal/terminal.h`; `terminal/style.h` is an independently includable focused styling header and is also included by the umbrella. Installed consumers link `GameWIP::Terminal`; source-tree consumers may link `Terminal`.

Terminal publicly depends on IO for statuses, flush modes, and byte-write results and privately reuses Unicode for strict scalar conversion and grapheme traversal. Logger uses Terminal for normal console output, but Terminal does not depend on Logger. Terminal owns managed standard-stream coordination, session/native-state restoration, key/event normalization, interactive line discipline, UTF-8/native conversion, capability discovery, styling, and primitive controls; history, completion, prompts, menus, multi-line widgets, logging policy, and game runtime behavior belong elsewhere.
