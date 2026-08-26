@page filesystem_unicode_paths Unicode paths

Public operations use `GameWIP::FileSystem::Types::Path`, currently an alias to `std::filesystem::path`.

## Explicit UTF-8 boundary

Constructing `std::filesystem::path` directly from narrow text happens before FileSystem can inspect the bytes and is not guaranteed to interpret them
as UTF-8 on every platform. Use:

```cpp
const auto pathResult = GameWIP::FileSystem::pathFromUtf8(utf8Text);
const auto textResult = GameWIP::FileSystem::pathToUtf8(pathResult.path);
```

`pathFromUtf8()` converts UTF-8 text to the native `Path` representation. `pathToUtf8()` converts a path's stored spelling to UTF-8.

Neither helper makes a path absolute, canonical, or normalized. `pathToUtf8()` does not promise portable separators or a portable path grammar; it
preserves the representation produced by `std::filesystem::path` for the current platform.

Invalid or unrepresentable text returns `EncodingFailed`; allocation failure returns `OutOfMemory`.

## Native operations

Once a `Path` reaches FileSystem, the backend performs native conversion. On Windows, this preserves Unicode paths without routing them through the
active narrow code page.

FileSystem intentionally does not add narrow-string overloads to every operation. Store paths as `Types::Path` and convert only at explicit text
boundaries.

## File contents

`readAllText()`, `writeAllText()`, `appendText()`, and `writeAllTextAtomic()` are strict UTF-8 content operations. Malformed or incomplete text
returns `EncodingFailed`. Text writes and appends validate before filesystem mutation; byte overloads remain available for arbitrary content.

Content helpers do not normalize text, add or remove a BOM, or convert line endings or encodings.

## Related pages

- @ref filesystem_path_operations
- @ref filesystem_whole_file_io
- @ref filesystem_troubleshooting
