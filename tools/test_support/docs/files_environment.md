@page test_support_files_environment TestSupport files and environment

These helpers favor compact test setup and cleanup. Use FileSystem or custom code when a test must preserve detailed status, native errors, encoding policy, durability, sharing, or atomicity.

## Text file helpers

### `readTextFile()`

The function opens in binary mode, determines an initial size, reads once, and returns the bytes in `std::string`. It performs no UTF-8 validation, BOM handling, or newline conversion.

It returns empty text when:

- the file is empty;
- the file cannot be opened;
- the initial position is non-positive.

Those outcomes cannot be distinguished from the return value. A short underlying stream read produces a shorter string. Allocation, length, path conversion, or stream exceptions may propagate.

### `writeTextFile()`

The function creates non-empty parent paths, opens in binary truncation mode, and replaces existing contents. It performs no encoding or line-ending conversion.

Filesystem setup, path conversion, allocation, open, and write failures can throw `std::filesystem::filesystem_error`, `std::runtime_error`, or another standard exception.

### Queries and cleanup

- `fileExists()` returns false for a missing path and for a filesystem-query error.
- `fileContains()` first requires `fileExists()`, then searches the ambiguous `readTextFile()` result. An empty substring can therefore succeed for an existing path whose read returned empty text.
- `countFileOccurrences()` counts non-overlapping matches. Empty search text returns zero; open failure and no matches are also indistinguishable.
- `createDirectories()` treats an empty path as a successful no-op and otherwise permits filesystem exceptions.
- `removeIfExists()` removes a complete tree and suppresses every removal error.

## Scoped temporary directory

`ScopedTemporaryDirectory` creates a unique child beneath the operating-system temporary directory. The purpose is sanitized into a readable filename prefix, and allocation uses a bounded collision-retry loop.

The object is non-copyable and non-movable. `path()` returns an object-owned reference valid until destruction. The destructor attempts recursive removal and then removes empty TestSupport parent directories. Cleanup failure is suppressed; open/locked resources, abnormal process termination, or external filesystem activity can leave artifacts.

Construction can throw when the temporary root cannot be resolved/created, a candidate cannot be created, collision attempts are exhausted, or formatting/allocation fails.

## Scoped current path

`ScopedCurrentPath` reads the current process directory and changes to the requested path during construction. `previousPath()` returns an object-owned reference. Destruction attempts restoration and suppresses failure.

The current directory is process-global. Safe use requires strict LIFO ownership and no unrelated relative-path resolution or direct current-directory mutation during the scope. TestSupport does not hold a process-wide lock for the scope lifetime.

## Environment guards

`ScopedEnvironmentVariable` copies a name and value, records the prior value, and restores it at destruction. `ScopedUnsetEnvironmentVariable` records the prior value and removes the variable temporarily.

Names must be non-empty and contain no `=`. Win32 narrow names and values are UTF-8, embedded nulls and invalid UTF-8 are rejected, and oversized conversion input can throw `std::length_error`.

Environment names are case-insensitive on Windows. Mutations and restoration operations performed through TestSupport are serialized, but a guard does not own the environment mutex for its lifetime. Overlapping scopes for the same logical name can therefore restore stale state in surprising order. Other environment APIs do not participate in the mutex, and environment mutation may invalidate pointers previously returned by `std::getenv()`.

On the Win32 backend, `ScopedEnvironmentVariable(name, "")` follows `_wputenv_s` semantics and removes the variable rather than representing a distinguishable empty value. Child-process overrides can represent an explicitly empty child value.

Guard destructors suppress restoration failures.

## Related pages

- @ref test_support_expectations
- @ref test_support_child_processes
- @ref test_support_troubleshooting
