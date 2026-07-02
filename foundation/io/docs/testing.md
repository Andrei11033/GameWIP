@page io_testing IO maintainer validation

@note This page is for maintainers. It describes proof coverage, not supported consumer API.

The focused IO suite covers:

- status helpers, defaults, and stable error names;
- Reader and Writer default, move, capability, seek, flush, close, and unsupported-operation contracts;
- MemoryReader reads, overlapping destinations, seeking, position, close state, string-view input, and temporary-storage rejection;
- MemoryWriter position, capacity reuse, close state, extraction, and aliased writes;
- known-size, unknown-size, exact-limit, over-limit, and zero-limit whole-stream reads;
- partial reads/writes, backend failures, invalid backend byte counts, and zero progress;
- caller-owned scratch buffers and allocation-conscious paths.

GameWIP owns the executable, module selection, CTest registration, reports, and coverage workflow. See @ref project_testing and @ref project_coverage.
