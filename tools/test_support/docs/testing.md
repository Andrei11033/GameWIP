@page test_support_testing TestSupport maintainer validation

@note This page describes proof coverage and environment requirements, not installed consumer API.

## Focused validation

The `test_support` module covers:

- `Runner`, `Context`, summaries, sections, exception conversion, and continuation;
- every expectation family and value-formatting fallback;
- report buffering, suite-boundary and immediate flushing, verbosity, append/truncate, and one-time sink-failure diagnostics;
- temporary-directory uniqueness, nested cleanup, current-path restoration, file-helper success and ambiguity cases;
- environment set/unset/restoration and invalid UTF-8/name input;
- child zero/nonzero exit, launch failure, timeout, capture limits, continued draining, environment blocks, descendant cleanup, and test-requested termination;
- manual-check EOF/skip behavior;
- timer, one-shot gate, stop flag, worker indexing, exceptions, and partial-startup cleanup.

Run it through the project workflow documented by @ref project_testing and @ref project_validation.

## Other validation boundaries

Project validation also checks:

- `test_support/test_support.h` as a self-contained C++23 public header;
- clean exact-version installed-package consumption through `GameWIP::TestSupport`;
- game validation child routes that use TestSupport process isolation;
- report modes and process-global state restoration;
- Doxygen warnings and local page references.

TestSupport must remain independent of IO, FileSystem, Terminal, Logger, Assert, engine, and game runtime code. It exposes no special source-tree test-hook header.

## Platform scope

The current environment and process backend is Win32. Portable tests should assert public result fields and state restoration rather than private native handles or implementation ordering.

## Documentation validation

The Doxygen warning log must be empty. Manual examples should compile against the installed public header and target. Maintainer comments explain backend safety and cleanup invariants without promoting internal mechanics to public guarantees.

See @ref project_documentation and @ref project_testing.
