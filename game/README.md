# Game executable and validation

This directory is where the reusable pieces become runnable programs. It owns
process startup, the game runtime facade, and the validation executables. The
behavior being composed still belongs in its engine, foundation, or tool
library rather than accumulating here.

## Layout

- `main.cpp` keeps process startup small and delegates runtime work.
- `runtime/` owns the game runtime facade and generated version header.
- `validation/tests/` owns modular correctness tests and their standalone runner.
- `validation/benchmarks/` owns performance measurements and registration smoke tests.
- `validation/public_headers/` proves supported consumer headers compile independently.
- `validation/installed_consumer/` validates installed CMake package boundaries.

## Find the right documentation

Read the [game executable manual](../docs/doxygen/game_executable.md) for startup,
composition, versioning, and source-interface rules. Use the
[command-line tools reference](../docs/doxygen/command_line_tools.md) for all
helper and executable commands. Use the
[validation architecture](../docs/doxygen/validation.md) and
[testing guide](../docs/doxygen/testing.md) before changing validation code.

Keep `main.cpp` small: it should establish process-level behavior and delegate.
Put new gameplay or engine behavior at the boundary that owns the concept, keep
platform-specific details behind an internal backend, and cover deterministic
behavior with focused correctness tests.

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
