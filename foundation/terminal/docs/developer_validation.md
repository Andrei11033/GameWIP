@page terminal_developer_validation Terminal developer validation

Run the project test executable through CTest:

```text
ctest --test-dir build-optimized-debuggable --output-on-failure
```

Terminal backend hooks are enabled with:

```text
GAMEWIP_ENABLE_TERMINAL_TEST_HOOKS=ON
```

Hook-enabled builds should cover deterministic capability, read, write, styling, control, and failure paths. The validation strategy is documented in @ref terminal_testing.
