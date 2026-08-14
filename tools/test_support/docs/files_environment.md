@page test_support_files_environment Files and environment

These helpers provide compact infrastructure contracts for validation fixtures. File helpers are in `test_support/files.h`; process-environment guards are in `test_support/process.h`.

## Status interpretation

`Types::InfrastructureStatus::ok()` is true only for `Types::InfrastructureError::None`. `nativeCode` preserves a platform or standard-library diagnostic when available. Success and code-only failures do not allocate diagnostic strings.

## Strict UTF-8 text files

`readTextFile()` and `writeTextFile()` are Text APIs and therefore enforce the project UTF-8 contract.

`readTextFile()` opens in binary mode so bytes are not transformed by the runtime, then validates the bytes once before exposing them as text.

- Empty file: success with empty `text`.
- Valid UTF-8: success with the exact bytes in `text`.
- Malformed or incomplete UTF-8 after successful I/O: `EncodingFailed` and the complete valid UTF-8 prefix in `text`.
- An I/O failure remains `FileOperationFailed`; any retained partial `text` is trimmed to a complete valid UTF-8 prefix before return.
- No normalization, BOM handling, or newline conversion occurs.

`writeTextFile()` validates the complete input before any filesystem mutation. Malformed or incomplete UTF-8 therefore returns `EncodingFailed` before parent-directory creation and before a truncating destination open. Existing destination content is not destroyed by malformed text input.

## Queries and cleanup

`fileExists()`, `fileContains()`, `countFileOccurrences()`, `createDirectories()`, and `removeIfExists()` retain their #54 operational-status semantics. `fileContains()` and `countFileOccurrences()` operate on text returned by `readTextFile()`, so they never search malformed returned text.

## Scoped filesystem state

`ScopedTemporaryDirectory` and `ScopedCurrentPath` remain non-copyable/non-movable inert-on-failure guards. Their constructors are `noexcept`; inspect `status()` before using the requested state. Destructors perform best-effort non-throwing cleanup/restoration.

## Environment guards

`ScopedEnvironmentVariable` and `ScopedUnsetEnvironmentVariable` are process-global state guards. They remain in the process surface because environment state is shared with child-process launch.

TestSupport serializes individual read/mutate/restore operations, not a guard's complete lifetime. Overlapping scopes still require caller coordination. On Win32, strict UTF-8/native conversion remains performed at the platform boundary without adding a redundant whole-string validation pass beforehand.

@ref test_support_public_api
@ref test_support_child_processes
