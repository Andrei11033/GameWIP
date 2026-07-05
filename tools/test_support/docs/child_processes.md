@page test_support_child_processes TestSupport child processes

Child process tests are built around `Types::ChildProcessOptions`, `Types::EnvironmentVariable`, `Types::ChildProcessResult`, and `runChildProcess()`.

Supported Windows behavior:

- executable path and argument vector;
- environment variable overrides and unsets;
- optional parent environment inheritance;
- timeout;
- optional stdout/stderr capture;
- bounded retained output with continued pipe draining;
- exit code, timeout state, test-requested termination state, and output truncation state.

Example:

```cpp
GameWIP::TestSupport::Types::ChildProcessOptions options;
options.executablePath = argv[0];
options.arguments = {"--child-mode"};
options.environment = {
    {"MYAPP_TEST_MODE", "1"},
    {"MYAPP_OPTIONAL_SETTING", std::nullopt},
};
options.timeout = std::chrono::milliseconds{5000};
options.maxCapturedOutputBytes = 64 * 1024;

const GameWIP::TestSupport::Types::ChildProcessResult result =
    GameWIP::TestSupport::runChildProcess(options);
```

These tests only require reliable reporting of successful exit, nonzero exit, timeout, and test-requested termination. TestSupport does not classify portable crash reasons beyond those observable results.

The default retained-output limit is `kDefaultMaxCapturedOutputBytes`. Bytes beyond the limit are discarded while the pipe continues to drain so a verbose child cannot deadlock. A zero limit retains no bytes. When `captureOutput` is false, the limit is ignored and `output` remains empty.

Executable paths, arguments, environment names, and environment values must not contain embedded nulls. Narrow process text must be valid UTF-8, and environment names must be non-empty and must not contain `=`. `runChildProcess()` throws `std::invalid_argument` for these caller errors instead of launching a child with substituted or truncated data.

On Win32, a child inherits only the selected standard-input, standard-output, and standard-error handles. Other inheritable parent handles are not exposed. Timeout, wait failure, and normal primary-process completion terminate remaining descendants before `runChildProcess()` returns, so descendant output handles cannot keep the parent blocked.
