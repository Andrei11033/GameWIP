@page io_testing IO testing

The IO library should be tested with focused unit tests covering memory readers, memory writers, whole-stream helpers, partial reads and writes, limits, capability queries, and failure propagation.

## Coverage

The IO suite covers:

- public status helpers, status defaults, and stable error names;
- Reader and Writer move/default/capability/seek contracts;
- MemoryReader reading, overlapping destinations, seeking, position, close state, string-view input, and temporary-storage rejection;
- MemoryWriter position, capacity reuse, close state, extraction, and aliased writes;
- known-size, unknown-size, exact-limit, over-limit, and zero-limit reads;
- partial reads, backend failures, invalid backend byte counts, and zero progress;
- partial writes, backend failures, and invalid backend byte counts.

GameWIP owns the executable, focused-module command, CTest registration, presets, report location, and coverage workflow. See @ref library_testing and @ref library_coverage.
