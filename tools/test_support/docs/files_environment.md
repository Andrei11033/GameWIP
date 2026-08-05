@page test_support_files_environment Files and environment

These helpers provide compact, non-throwing infrastructure contracts for test fixtures. Each operation returns a TestSupport-owned `Types::InfrastructureStatus`; value-producing operations return that status beside their domain value. Use FileSystem when a test needs production filesystem policy, durability, sharing, atomicity, or richer operation-specific results.

## Status interpretation

`Types::InfrastructureStatus::ok()` is true only when `error` is `Types::InfrastructureError::None`. `nativeCode` preserves a platform or standard-library diagnostic when available; zero means no numeric diagnostic was available. Status creation and inspection do not allocate.

Caller-side construction of `std::filesystem::path`, `std::string`, and other allocating arguments remains governed by those standard-library types. Once control enters a helper, expected filesystem, environment, conversion, and implementation-allocation failures are converted to status.

## Text file helpers

`readTextFile()` opens in binary mode, determines an initial size, reads the bytes, and returns `Types::TextResult`. It performs no UTF-8 validation, BOM handling, or newline conversion.

- An empty file returns successful status and empty `text`.
- Open, positioning, or read failure returns `FileOperationFailed`.
- Implementation-owned allocation failure returns `OutOfMemory`.
- A failed read may retain bytes read before the failure.

`writeTextFile()` creates non-empty parent paths, opens in binary truncation mode, writes the supplied bytes, and flushes the stream. It returns status and performs no encoding or line-ending conversion.

## Queries and cleanup

- `fileExists()` returns successful false for absence, successful true for existence, and failed status for an inspection error.
- `fileContains()` returns the `readTextFile()` status plus whether the substring appears. An empty substring succeeds for every successfully read file, including an empty file.
- `countFileOccurrences()` returns the read status plus a non-overlapping count. Empty search text produces successful zero only when the file was read successfully.
- `createDirectories()` returns success for an empty path, an existing directory tree, or successful creation.
- `removeIfExists()` returns success when the path is absent or the file/tree was removed. Removal errors are reported instead of being mistaken for successful cleanup.

## Scoped temporary directory

`ScopedTemporaryDirectory` attempts to create a unique child beneath the operating-system temporary directory. The purpose is sanitized into a readable filename prefix, and allocation uses a bounded collision-retry loop.

Construction is `noexcept`. Inspect `status()` before using `path()`. Failed construction leaves an inert guard with an empty path. The object is non-copyable and non-movable.

The destructor attempts recursive removal and then removes empty TestSupport parent directories. Cleanup is best effort and cannot be reported from the destructor; open resources, abnormal termination, or external filesystem activity can leave artifacts.

## Scoped current path

`ScopedCurrentPath` attempts to capture the current process directory and change to the requested path. Construction is `noexcept`; inspect `status()` before relying on the change. Failed construction leaves an inert guard with an empty `previousPath()`.

The current directory is process-global. Safe use requires strict LIFO ownership and no unrelated relative-path resolution or direct current-directory mutation during the scope. Successful guards attempt best-effort restoration during `noexcept` destruction.

## Environment guards

`ScopedEnvironmentVariable` attempts to copy a name, record its prior value, and set a temporary value. `ScopedUnsetEnvironmentVariable` attempts to record the prior value and remove the variable. Both constructors are `noexcept`; inspect `status()` before relying on the mutation. Failed guards are inert.

Names must be non-empty and contain no `=`. Win32 narrow names and values are UTF-8; embedded nulls, invalid UTF-8, and oversized conversion input return `InvalidArgument`. Native environment failures return `EnvironmentFailed`, and implementation-owned allocation failure returns `OutOfMemory`.

Environment names are case-insensitive on Windows. TestSupport serializes each mutation or restoration operation, not the guard lifetime. Overlapping scopes can restore stale state in surprising order, other environment APIs do not participate in the mutex, and mutation may invalidate pointers previously returned by `std::getenv()`.

On Win32, `ScopedEnvironmentVariable(name, "")` follows `_wputenv_s` removal semantics. Child-process overrides can represent an explicitly empty child value.

Successful guard destructors attempt restoration and suppress any restoration failure.

## Related pages

- @ref test_support_expectations
- @ref test_support_child_processes
- @ref test_support_troubleshooting
