@page filesystem_path_operations FileSystem path operations

FileSystem exposes both lexical path transformations and operations that consult process or filesystem state. They are deliberately separate.

## Lexical operations

The following operations use `std::filesystem::path` component rules and do not access the filesystem:

- `parentPath()`;
- `filename()`;
- `stem()`;
- `extension()`;
- `replaceExtension()`;
- `joinPath()`;
- `isAbsolutePath()`;
- `isRelativePath()`.

They do not require the path to exist and do not normalize, canonicalize, or verify components.

`joinPath()` follows `std::filesystem::path::operator/=` semantics. A rooted or absolute right-hand path can replace part or all of the left-hand path according to the native path grammar; it is not guaranteed to be a simple separator-and-append operation.

Empty paths are valid inputs to lexical operations and produce the corresponding standard path result.

## Process current directory

`getCurrentDirectory()` reads process-wide state. `setCurrentDirectory()` requires an existing directory and changes the process current directory for every thread and library in the process.

Do not use process current-directory mutation as thread-local context. Prefer stable absolute paths or resolve relative paths at a controlled boundary.

## Absolute and canonical paths

`absolutePath()` makes a relative path absolute using the current working directory. It does not require the target to exist and does not promise canonical or lexically normalized spelling.

`canonicalPath()` resolves the complete path, including ordinary filesystem symlink resolution, and requires every component and the final target to exist.

`weaklyCanonicalPath()` canonicalizes the existing prefix and permits missing trailing components.

These functions use the standard filesystem's resolution behavior and do not accept `SymlinkPolicy`. Use the policy-bearing FileSystem operations when the strict traversal contract matters.

Empty paths return `InvalidArgument` for absolute and canonical resolution.

## Temporary directory

`getTemporaryDirectoryPath()` queries the platform-selected temporary directory. It does not create the directory, reserve a filename, or guarantee that the returned directory remains available or writable after the call.

## Concurrency and state changes

Path results are snapshots. The filesystem, current directory, mount layout, and symlink targets can change after a successful query. Do not treat canonicalization or existence checks as authorization for a later path reopen; use an operation whose symlink policy enforces the required traversal contract.

## Related pages

- @ref filesystem_unicode_paths
- @ref filesystem_symlink_policies
- @ref filesystem_metadata
