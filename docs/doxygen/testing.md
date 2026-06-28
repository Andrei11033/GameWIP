@page library_testing Correctness testing

Correctness tests answer whether behavior is right. They must not contain benchmark loops, timing thresholds, or performance-regression policy.

## Run tests

```powershell
cmake --preset validation
cmake --build --preset validation
ctest --preset validation
```

Run all modules directly:

```powershell
.\build-validation\GameWIPTests.exe --no-manual-ui
```

Run one module:

```powershell
.\build-validation\GameWIPTests.exe `
  --test-module=filesystem `
  --test-report=logs/tests/filesystem_test_report.txt
```

The development preset links the same modules into `GameWIP`, where they run before game startup.

## Module standard

Each module owns a directory under `game/validation/tests` containing:

- an explicit `CMakeLists.txt`;
- its test implementation and local option header;
- a small `module.cpp` registration adapter.

Register sources and dependencies with:

```cmake
gamewip_add_test_module(
    NAME filesystem
    SOURCES
        filesystem_test.cpp
        module.cpp
    LINK_LIBRARIES
        FileSystem
        TestSupport
)
```

The C++ registration name must match the CMake module name. Give the module a stable order and add a child-argument matcher only when it owns a child-process protocol.

## Test requirements

- Tests are deterministic, order-independent, and safe to run repeatedly.
- Default CTest runs do not open UI or wait for input.
- Temporary files use isolated directories and are removed by scoped cleanup.
- Global process state is restored before a module returns.
- Child-process modes route to exactly one owning module.
- New behavior and bug fixes receive focused regression coverage when practical.
- Sleep-based synchronization is avoided; bounded timeouts protect unavoidable process and concurrency waits.
- Report failures do not hide console results or change the behavior under test.
- Test hooks are compiled only when tests are built or embedded.

Long-running stress scenarios may remain correctness tests when they verify invariants rather than compare speed. Use @ref project_benchmarking for throughput and latency measurements.
