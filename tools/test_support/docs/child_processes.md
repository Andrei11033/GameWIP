@page test_support_child_processes TestSupport child processes

Child process tests are built around `Types::ChildProcessOptions`, `Types::EnvironmentVariable`, `Types::ChildProcessResult`, and `runChildProcess()`.

Supported Windows behavior:

- executable path and argument vector;
- environment variable overrides and unsets;
- optional parent environment inheritance;
- timeout;
- optional stdout/stderr capture;
- exit code, timeout state, and test-requested termination state.

Example:

```cpp
GameWIP::TestSupport::Types::ChildProcessOptions options;
options.executablePath = argv[0];
options.arguments = {"--child-mode"};
options.environment = {
    {"GAMEWIP_TEST_MODE", "1"},
    {"GAMEWIP_IGNORE_PARENT_VALUE", std::nullopt},
};
options.timeout = std::chrono::milliseconds{5000};

const GameWIP::TestSupport::Types::ChildProcessResult result =
    GameWIP::TestSupport::runChildProcess(options);
```

GameWIP tests only require reliable reporting of successful exit, nonzero exit, timeout, and test-requested termination. The TestSupport library does not classify portable crash reasons beyond those observable results.
