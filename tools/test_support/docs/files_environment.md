@page test_support_files_environment TestSupport files and environment

## Text file helpers

These helpers are intentionally small and text-oriented. Use custom code for binary fixtures, structured parsing, or tests that need detailed I/O error reporting.

`readTextFile()` returns empty text on open failure, so use `fileExists()` when an empty file and an unreadable file must be distinguished. Search helpers return false for missing files, including an empty search. Occurrence counting is non-overlapping and treats an empty needle as zero.

`writeTextFile()` creates parent directories and throws on open or write failure. Cleanup helpers suppress filesystem errors so teardown does not hide the test result.

## Scoped temporary directories

`ScopedTemporaryDirectory` creates a unique directory beneath the operating-system temporary directory and removes the complete tree when its scope ends. Use it for test inputs, generated files, captured logs, and other artifacts that must not appear in a repository or build directory.

```cpp
{
    GameWIP::TestSupport::ScopedTemporaryDirectory workspace("parser_tests");
    const std::filesystem::path input = workspace.path() / "input.txt";
    GameWIP::TestSupport::writeTextFile(input, "fixture");
    runParserTest(input);
} // The workspace and every contained artifact are removed.
```

Names are unique across concurrent scopes. The readable purpose is sanitized for use as a path prefix. Construction throws when the OS temporary directory cannot be resolved or a unique directory cannot be created; destruction suppresses cleanup errors so teardown cannot replace the test outcome.

`ScopedCurrentPath` temporarily changes the process working directory and restores the previous path on destruction. It is useful when testing APIs whose documented contract intentionally uses relative paths:

```cpp
GameWIP::TestSupport::ScopedTemporaryDirectory workspace("default_path_test");
{
    GameWIP::TestSupport::ScopedCurrentPath currentPath(workspace.path());
    runApiThatUsesRelativePaths();
}
```

The working directory is process-global. Do not overlap `ScopedCurrentPath` instances or use one while unrelated threads resolve relative paths. Stop subsystem workers before restoring the path.

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
