@page terminal Terminal

`GameWIP::Terminal` provides platform-neutral UTF-8 and byte standard-stream I/O, persistent managed input sessions, structured event contracts,
capability discovery, styling, and low-level terminal controls.

Terminal is a shared runtime library. One process-wide implementation coordinates standard-stream access across the application and shared libraries
such as Logger. Callers may use direct one-operation functions or create a move-only `Session` when interactive input needs persistent ownership and
native state.

The focused public headers are `terminal/types.h`, `terminal/style.h`,
`terminal/input.h`, `terminal/output.h`, and `terminal/session.h`.
`terminal/terminal.h` remains the complete convenience include.

## How the library is organized

Direct functions temporarily acquire the required stream, perform one
operation, and restore native state before returning. A `Session` keeps stdin
ownership across several interactive operations and can track reversible output
state. Both forms share the same stdout/stderr serialization and capability
model. Text operations accept or return valid UTF-8; byte operations deliberately
make no encoding promise. Structured events are the common foundation beneath
immediate key input and line editing.

## Consumer manual

- @subpage terminal_quick_start — Include, link, print UTF-8 text, inspect
  capabilities, and read a line.
- @subpage terminal_public_api — Find direct operations, sessions, events,
  styles, options, buffers, scopes, and results.
- @subpage terminal_abi — Understand the shared-library, export, and runtime
  boundary.
- @subpage terminal_read_write — Understand text versus byte I/O, blocking,
  partial progress, flushing, and line input.
- @subpage terminal_segmented_writes — Emit mixed text, style, and control
  segments as one coordinated write.
- @subpage terminal_styling — Apply colors and attributes and restore output
  state safely.
- @subpage terminal_capabilities_and_redirection — Adapt to consoles, pipes,
  files, detached streams, and changing endpoint support.
- @subpage terminal_input_modes — Choose immediate events, stream reads, or
  edited lines and understand stdin ownership.
- @subpage terminal_control_primitives — Move the cursor, clear regions, set
  titles, and use other low-level terminal operations.
- @subpage terminal_unicode_io — Follow UTF-8 validation, native conversion,
  grapheme editing, and embedded-NUL rules.
- @subpage terminal_examples — See output, sessions, events, styling, and
  capability fallbacks in context.
- @subpage terminal_troubleshooting — Diagnose redirection, contention,
  unsupported operations, invalid text, and state-restoration problems.

## Maintainer validation

- @subpage terminal_testing — See automated, manual, package, and concurrency
  coverage.
- @subpage terminal_test_hooks — Understand source-tree-only terminal fault
  seams and reset requirements.

## Generated API reference

Use @ref GameWIP::Terminal for direct operations, `Session`, factories, formatted output, buffers, and output-state RAII scopes. Use @ref
GameWIP::Terminal::Types for streams, session policies, events, capabilities, styles, options, segments, and result types.

The generated pages contain exact declarations and overloads from the
`terminal/terminal.h` umbrella and the focused `terminal/style.h` styling
header. The guides explain how to choose among them and cover endpoint-dependent
behavior, ownership, failures, concurrency, packaging, and caveats.

## Key behavior

Public Text is complete valid UTF-8 on every endpoint. Real-console output is
converted through the native Unicode console API; redirected text preserves the
validated UTF-8 bytes unchanged. Raw byte operations make no text guarantee.

Native Win32 console input is decoded from structured `INPUT_RECORD` values into
portable logical events. Interactive Stream reads and Unicode line editing are
built on that same event engine, so the two delivery modes do not develop
different key behavior.

Terminal has one process-wide managed stdin ownership domain. A persistent
`Session` owns stdin until `close()`, binds one primary output, and tracks any
reversible output state it explicitly changes. A direct read acquires the same
ownership temporarily and restores exact native state before releasing it.

Direct and Session output share independent stdout and stderr serialization, so
output can continue while a Session read blocks. Access through C streams, C++
iostreams, native handles, or third-party terminal libraries bypasses this
coordination.

Capabilities depend on the current endpoint. Real terminals, redirected streams, detached streams, and other handles can support different operations.
Capability queries are snapshots; the status returned by the requested operation remains authoritative.

## Dependency boundary

The normal public umbrella is `terminal/terminal.h`; `terminal/style.h` is an independently includable focused styling header and is also included by
the umbrella. Installed consumers link `GameWIP::Terminal`; source-tree consumers may link `Terminal`.

Terminal publicly depends on IO for statuses, flush modes, and byte-write
results. It privately reuses Unicode for strict scalar conversion and grapheme
traversal. Logger uses Terminal for normal console output; Terminal does not
depend on Logger.

Terminal stops at low-level standard-stream behavior: coordination, native-state
restoration, key/event normalization, line discipline, text conversion,
capability discovery, styling, and primitive controls. History, completion,
prompts, menus, multi-line widgets, logging policy, and game runtime behavior
belong in higher-level components.
