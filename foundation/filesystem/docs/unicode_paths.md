@page foundation_filesystem_unicode_paths FileSystem Unicode Paths

This page documents planned path and text boundary behavior for `GameWIP::FileSystem`.

No FileSystem backend is implemented in this pass.

## Public path type

Public paths remain:

```cpp
std::filesystem::path
```

The public API stays platform-neutral. Callers do not pass Win32 handles, UTF-16 buffers, or platform-specific path structures.

## Windows backend rule

The Windows backend converts paths and text at the platform boundary and uses explicit Unicode Win32 APIs.

Use explicit `W` APIs such as:

```cpp
CreateFileW
ReadFile
WriteFile
FlushFileBuffers
MoveFileExW
CopyFileW
ReplaceFileW
CreateDirectoryW
RemoveDirectoryW
DeleteFileW
```

Do not use ANSI Win32 APIs or generic macro-mapped A/W APIs.

## Text files

Public text remains UTF-8 `std::string` and `std::string_view`.

FileSystem text helpers treat text as UTF-8 bytes. They do not parse or validate JSON, config, controls, save, or asset formats.

## Backend scope

The FileSystem backend owns local filesystem OS calls only. Terminal output, debugger output, popups, dialogs, and child-process behavior belong to other libraries.
