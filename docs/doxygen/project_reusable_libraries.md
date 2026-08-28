@page project_reusable_libraries Reusable libraries

These are the supported, documented C++ libraries in GameWIP. Every entry has a
quick start, conceptual guides, examples, troubleshooting, validation notes,
and generated reference pages for its public symbols.

Start with a library's landing page when you need to understand its model or
decide whether it is the right dependency. Use its generated namespace and
class pages while coding, when the exact contract of a function or type matters.
The dependency graph and repository ownership rules are in @ref
project_structure.

## Foundation libraries

- @subpage unicode — Strict Unicode validation, conversion, scalar traversal,
  and grapheme traversal.
- @subpage io — Shared byte-reader, byte-writer, transfer, status, and UTF-8
  text contracts.
- @subpage filesystem — Paths, files, directories, metadata, locking, sharing,
  and atomic replacement.
- @subpage terminal — Standard-stream I/O, terminal sessions and events,
  styling, capabilities, and control operations.

## Internal foundation support

- @subpage internal_base — Source-tree-only checked arithmetic and typed Win32
  procedure lookup shared by independent implementations.

## Engine libraries

- @subpage window_library — Native desktop-window ownership, events, displays,
  Clipboard data exchange, coordinates, fullscreen behavior, and
  renderer/native integration.

## Tool libraries

- @subpage logger — Asynchronous diagnostics, synchronous emergency reports,
  sinks, filtering, health, and lifecycle.
- @subpage assert — Fatal assertions, recoverable checks, debug breaks, and
  optional interactive failure handling.
- @subpage test_support — Reusable test runners, expectations, reports,
  fixtures, process isolation, manual checks, and stress helpers.

## Libraries still under design

Input and Action compile in the source tree, but their public boundaries are
still provisional. WindowManager is preserved historical code and is not
compiled. They are listed in @ref project_structure and the roadmap so readers
can understand their status, but they are deliberately excluded from the
supported API and library manuals until their contracts stabilize.

## Related pages

- @ref project_structure
- @ref project_extending
- @ref project_library_compatibility
