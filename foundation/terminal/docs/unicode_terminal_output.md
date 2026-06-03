@page foundation_terminal_unicode_terminal_output Terminal Unicode Output

This page documents planned Unicode output behavior for `GameWIP::Terminal`.

No Terminal backend is implemented in this pass.

## Public text

Public text uses UTF-8 `std::string` and `std::string_view`.

Terminal text APIs treat public text as UTF-8. They do not parse JSON, config, save, asset, or markup formats.

## Windows console output

On Windows, real console output should convert UTF-8 public text to UTF-16 and use:

```cpp
WriteConsoleW
```

The backend should choose this path only when stdout or stderr is attached to a real console handle that accepts console text output.

## Windows redirected output

On Windows, redirected stdout and stderr output should remain byte-oriented and use:

```cpp
WriteFile
```

Redirected process output carries bytes. The Terminal backend should not force redirected output through UTF-16 console APIs.

## Other Windows APIs

Terminal does not own debugger output, popups, dialogs, or assertion UI.

Those behaviors remain in Logger, Assert, or other specialized libraries.

## Explicit API rule

Windows backend code must use explicit Unicode Win32 APIs where text APIs are needed. Do not use ANSI APIs or generic macro-mapped A/W APIs.
