# Game executable and validation

This directory owns the GameWIP process boundary. It composes engine and
reusable-library code; reusable behavior itself belongs in `engine/`,
`foundation/`, or `tools/`.

## Layout

- `main.cpp` keeps process startup small and delegates runtime work.
- `runtime/` owns the current game runtime facade and generated version header.
- `validation/tests/` owns modular correctness tests and their standalone runner.
- `validation/benchmarks/` owns performance measurements and registration smoke tests.
- `validation/public_headers/` proves supported consumer headers compile independently.
- `validation/installed_consumer/` validates installed CMake package boundaries.

## Where to start

Read the [game executable manual](../docs/doxygen/game_executable.md) for startup,
composition, versioning, and source-interface rules. Use the
[validation architecture](../docs/doxygen/validation.md) and
[testing guide](../docs/doxygen/testing.md) before changing validation code.

New gameplay or engine behavior should not accumulate in `main.cpp`. Add it to
the owning engine/game runtime boundary, keep platform-specific behavior behind
an internal backend, and add focused correctness coverage where practical.

## Common checks

```powershell
cmake --preset test
cmake --build --preset test
ctest --preset test
```

Run a focused module with:

```powershell
.\build\test\GameWIPTests.exe --test-module=<module>
```

The root [contributor workflow](../CONTRIBUTING.md) defines issue, pull-request,
validation-evidence, and merge-message expectations.
