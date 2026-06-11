@page foundation_filesystem_unicode_paths FileSystem Unicode paths

Public operations use `GameWIP::FileSystem::Types::Path` everywhere. It is currently an alias to `std::filesystem::path`.

Once a `Path` reaches FileSystem, the backend performs all required native conversion automatically. Windows uses the path's wide representation and explicit Unicode Win32 APIs.

Constructing a `std::filesystem::path` from narrow text happens before FileSystem can inspect it and is not guaranteed to interpret the bytes as UTF-8 on Windows. Use the explicit boundary helpers:

```cpp
const auto pathResult = GameWIP::FileSystem::pathFromUtf8(utf8Text);
const auto textResult = GameWIP::FileSystem::pathToUtf8(pathResult.path);
```

These are the only explicit conversions callers normally need. FileSystem does not add narrow-string overloads to every path operation and does not require application code to call `std::filesystem` conversion functions.

File text helpers treat text as UTF-8 bytes. They do not add or remove a BOM and do not perform encoding conversion.
