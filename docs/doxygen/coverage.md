@page library_coverage Coverage

Coverage is a build feature controlled by `ENABLE_LIBRARY_COVERAGE`. It is not a runtime `TestRunOptions` feature.

## Build and run

```powershell
cmake -S . -B build-coverage -G Ninja `
  -DASSERT_ENABLED=ON `
  -DASSERT_CHECKS_ENABLED=ON `
  -DLOGGER_TEST_HOOKS=ON `
  -DASSERT_TEST_HOOKS=ON `
  -DTERMINAL_TEST_HOOKS=ON `
  -DENABLE_LIBRARY_COVERAGE=ON

cmake --build build-coverage
.\build-coverage\GameWIP.exe
cmake --build build-coverage --target coverage
```

## Output

```text
build-coverage/coverage/index.html
build-coverage/coverage/coverage.xml
```

## gcovr behavior

When `gcovr` is found, the `coverage` target generates HTML and XML reports. When it is missing, the `coverage` target still exists and fails with a helpful message:

```text
python -m pip install gcovr
```

## gcov negative-hit warnings

Some GCC/gcov versions can emit negative branch hit counts for valid programs. This is a known gcov data issue, not automatically a library bug. The coverage target passes:

```text
--gcov-ignore-parse-errors negative_hits.warn_once_per_file
```

so gcovr still generates the HTML and XML reports while printing one warning per affected file. Review the warning list, but do not treat this specific parser warning as a failed test unless it is accompanied by missing report files or other gcovr errors.

Coverage/test-hook builds are for validation, not performance baselines.
