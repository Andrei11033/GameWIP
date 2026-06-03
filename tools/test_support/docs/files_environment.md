@page test_support_files_environment TestSupport files and environment

This page explains the file helpers and scoped environment helpers.

## Text file helpers

| API | Behavior |
| --- | --- |
| `readTextFile(path)` | Reads an entire text file. Returns empty text when it cannot be opened. |
| `writeTextFile(path, text)` | Creates parent directories and writes text. Throws when the file cannot be opened or written. |
| `fileExists(path)` | Returns whether the path exists without throwing. |
| `fileContains(path, text)` | Returns true only when the file exists and contains the text. Missing files return false, including for empty search text. |
| `countFileOccurrences(path, text)` | Counts non-overlapping occurrences. Empty search text returns zero. |
| `createDirectories(path)` | Creates a directory tree when the path is non-empty. |
| `removeIfExists(path)` | Removes a file or directory tree when present and ignores cleanup errors. |

These helpers are intentionally small and text-oriented. Use custom code for binary fixtures, structured parsing, or tests that need detailed I/O error reporting.

## Scoped environment variables

`ScopedEnvironmentVariable` temporarily sets an environment variable and restores the previous state on destruction:

```cpp
{
    GameWIP::TestSupport::ScopedEnvironmentVariable variable("GAMEWIP_TEST_MODE", "1");
    runScenarioThatReadsEnvironment();
}
```

`ScopedUnsetEnvironmentVariable` temporarily unsets a variable and restores it later:

```cpp
{
    GameWIP::TestSupport::ScopedUnsetEnvironmentVariable variable("GAMEWIP_OPTIONAL_SETTING");
    runScenarioWithoutSetting();
}
```

Environment variables are process-global. Avoid overlapping scoped environment changes for the same name across threads.

## Windows behavior

On Windows, TestSupport updates both:

- the CRT environment used by `std::getenv()`, and
- the process environment inherited by child processes.

Windows backend code follows the project Unicode standard: public text uses UTF-8 `std::string`, and Win32 platform calls convert to UTF-16 and call explicit `W` APIs.
