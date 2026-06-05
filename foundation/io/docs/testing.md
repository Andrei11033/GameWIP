@page io_testing IO testing

The IO library should be tested with focused unit tests covering memory readers, memory writers, whole-stream helpers, partial reads and writes, limits, capability queries, and failure propagation.

## Focused run

Build the normal optimized-debuggable target and run only IO tests:

```powershell
cmake --build build-optimized-debuggable --target GameWIP
.\build-optimized-debuggable\GameWIP.exe --io-only
```

The focused suite covers:

- public status helpers, status defaults, and stable error names;
- Reader and Writer move/default/capability/seek contracts;
- MemoryReader reading, overlapping destinations, seeking, position, close state, string-view input, and temporary-storage rejection;
- MemoryWriter position, capacity reuse, close state, extraction, and aliased writes;
- known-size, unknown-size, exact-limit, over-limit, and zero-limit reads;
- partial reads, backend failures, invalid backend byte counts, and zero progress;
- partial writes, backend failures, and invalid backend byte counts.

## CTest entry

The project CTest entry runs the normal automated test executable with its configured runtime suite selection:

```powershell
ctest --test-dir build-optimized-debuggable --output-on-failure
```

## Coverage

Configure with `GAMEWIP_ENABLE_COVERAGE=ON` and use the project coverage target when validating line and branch coverage. See @ref gamewip_coverage for the full workflow.
