@page project_coverage Coverage workflow

Coverage is an opt-in validation build. It is separate from runtime test selection and requires standalone tests.

```powershell
cmake --preset coverage
cmake --build --preset coverage
ctest --preset coverage
cmake --build build-coverage --target coverage
```

`GAMEWIP_ENABLE_COVERAGE=ON` adds GCC/Clang coverage instrumentation and creates the `coverage` target. The target uses `gcovr` to write:

```text
build-coverage/coverage/index.html
build-coverage/coverage/coverage.xml
```

The report includes foundation, tool, TestSupport, and modular correctness-test sources. Third-party sources under `external` are excluded.

GCC profile updates are atomic so parallel test processes cannot overwrite one another's counters. Corrupt or negative profile data is a report failure; the workflow does not suppress parser errors.

The Validation workflow runs this preset on Windows and uploads the HTML/XML output as a build artifact.

Coverage answers which correctness paths executed. It does not replace assertions, benchmark measurements, or manual UI validation.
