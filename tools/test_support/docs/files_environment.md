@page test_support_files_environment TestSupport files and environment

## Text file helpers

These helpers are intentionally small and text-oriented. Use custom code for binary fixtures, structured parsing, or tests that need detailed I/O error reporting.

`readTextFile()` returns empty text on open failure, so use `fileExists()` when an empty file and an unreadable file must be distinguished. Search helpers return false for missing files, including an empty search. Occurrence counting is non-overlapping and treats an empty needle as zero.

`writeTextFile()` creates parent directories and throws on open or write failure. Cleanup helpers suppress filesystem errors so teardown does not hide the test result.

## Scoped environment variables

`ScopedEnvironmentVariable` temporarily sets an environment variable and restores the previous state on destruction:

```cpp
{
    GameWIP::TestSupport::ScopedEnvironmentVariable variable("INTERNAL_TEST_SUPPORT_TEST_MODE", "1");
    runScenarioThatReadsEnvironment();
}
```

`ScopedUnsetEnvironmentVariable` temporarily unsets a variable and restores it later:

```cpp
{
    GameWIP::TestSupport::ScopedUnsetEnvironmentVariable variable("INTERNAL_TEST_SUPPORT_OPTIONAL_SETTING");
    runScenarioWithoutSetting();
}
```

Environment variables are process-global. Avoid overlapping scoped environment changes for the same name across threads.

## Windows behavior

On Windows, TestSupport updates both:

- the CRT environment used by `std::getenv()`, and
- the process environment inherited by child processes.

Windows backend code follows the project Unicode standard: public text uses UTF-8 `std::string`, and Win32 platform calls convert to UTF-16 and call explicit `W` APIs.
