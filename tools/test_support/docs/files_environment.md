@page test_support_files_environment Files and environment

These helpers provide compact infrastructure contracts for validation fixtures. File helpers are in `test_support/files.h`; process-environment guards
are in `test_support/process.h`.

## Status interpretation

`Types::InfrastructureStatus::ok()` is true only for `Types::InfrastructureError::None`. `nativeCode` preserves a platform or standard-library
diagnostic when available. Success and code-only failures do not allocate diagnostic strings.

## Strict UTF-8 text files

`readTextFile()` and `writeTextFile()` are Text APIs and therefore enforce the project UTF-8 contract.

`readTextFile()` opens in binary mode so bytes are not transformed by the runtime, then validates the bytes once before exposing them as text.

- Empty file: success with empty `text`.
- Valid UTF-8: success with the exact bytes in `text`.
- Malformed UTF-8 reports `EncodingFailed` and preserves the complete valid UTF-8 prefix, including when a backend failure is observed at the same
  read boundary.
- An incomplete final UTF-8 sequence at a definitive end reports `EncodingFailed`; a known-size early EOF therefore reports encoding failure when the
  retained suffix is incomplete.
- If a backend fails after returning a valid prefix followed only by an incomplete sequence, the backend `FileOperationFailed` remains authoritative
  because additional bytes may have existed.
- A valid but short read relative to the size measured before the read reports `FileOperationFailed` rather than silently succeeding with a prefix.
- Every returned `text` field is valid UTF-8, including on failure. No normalization, BOM handling, or newline conversion occurs.

`writeTextFile()` validates the complete input before any filesystem mutation. Malformed or incomplete UTF-8 therefore returns `EncodingFailed` before
parent-directory creation and before a truncating destination open. Existing destination content is not destroyed by malformed text input.

## Queries and cleanup

`fileExists()`, `fileContains()`, `countFileOccurrences()`, `createDirectories()`, and `removeIfExists()` report their operational status explicitly.
`fileContains()` and `countFileOccurrences()` operate on text returned by `readTextFile()`, so they never search malformed returned text.

- `fileExists()` returns successful false for absence, successful true for existence, and failed status for an inspection error.
- `fileContains()` requires a successful strict-text read; an empty substring succeeds only when that read succeeds.
- `countFileOccurrences()` counts non-overlapping matches. Empty search text produces successful zero only after a successful read.
- `createDirectories()` treats an empty path and an already-existing directory tree as successful no-ops.
- `removeIfExists()` treats absence as success and reports removal errors instead of collapsing them into successful cleanup.

## Scoped filesystem state

`ScopedTemporaryDirectory` and `ScopedCurrentPath` remain non-copyable/non-movable inert-on-failure guards. Their constructors are `noexcept`; inspect
`status()` before using the requested state. Destructors perform best-effort non-throwing cleanup/restoration.

`ScopedTemporaryDirectory` creates a unique child below the operating-system temporary directory using a sanitized readable purpose plus bounded
collision retries. Its destructor attempts recursive removal and then removes empty TestSupport parent directories; open handles, abnormal
termination, or external filesystem activity can still leave artifacts.

`ScopedCurrentPath` captures and changes process-global current-directory state. Safe use requires strict LIFO ownership and no unrelated
relative-path resolution or direct current-directory mutation during the scope.

## Environment guards

`ScopedEnvironmentVariable` and `ScopedUnsetEnvironmentVariable` are process-global state guards. They remain in the process surface because
environment state is shared with child-process launch.

Names must be non-empty and contain no `=`. On Win32, malformed UTF-8, embedded nulls, and oversized conversion input are rejected as
`InvalidArgument`; native environment failures use `EnvironmentFailed`, and implementation-owned allocation failure uses `OutOfMemory`. Setting a
scoped value to an empty string follows `_wputenv_s` removal semantics, while child-process environment overrides can represent an explicitly empty
child value.

TestSupport serializes individual read/mutate/restore operations, not a guard's complete lifetime. Overlapping scopes still require caller
coordination. Other environment APIs do not participate in the mutex, and mutation can invalidate pointers previously returned by `std::getenv()`. On
Win32, strict UTF-8/native conversion remains performed at the platform boundary without adding a redundant whole-string validation pass beforehand.

@ref test_support_public_api
@ref test_support_child_processes
