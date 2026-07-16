@page terminal_testing Terminal maintainer validation

@note This page describes validation coverage and commands. Source-tree hook APIs are documented separately in @ref terminal_test_hooks.

## Automated coverage

The Terminal validation module covers:

- public constants, value types, factories, deleted dangling-storage overloads, and header self-containment;
- stdout/stderr text, line, byte, formatted, segmented, and buffered writes;
- same-stream formatter reentry for `print()` and `println()`;
- complete-operation serialization, independent stdout/stderr state, and reusable scratch storage;
- capability observation, preparation, idempotence, and failure propagation;
- `StyleMode::Never`, `Auto`, and `Required`;
- redirected output, native line endings, UTF-8 conversion, and invalid-option rejection before normal emission;
- stdin line, text, and byte reads, code-point-safe limits, timeouts, would-block, EOF, and availability;
- input-mode query, set, default restore, scoped restore, and preservation of Terminal-buffered input;
- cursor, alternate screen, title, bell, clear, scroll, size, position, and flush behavior;
- scope setup, nesting, move behavior, explicit restoration, failed restoration, and retry;
- Logger integration through the shared Terminal runtime;
- internal backend boundaries and forced failure paths.

## Project validation layers

Terminal participates in:

- the focused Terminal validation module;
- public-header self-containment validation;
- installed-package consumer validation through `GameWIP::Terminal`;
- Logger integration validation;
- child-process validation for reentrant formatting paths;
- opt-in manual real-terminal validation.

GameWIP owns module selection, CTest registration, reports, and sanitizer workflows. See @ref project_testing and @ref project_validation.

## Real-terminal validation

Automated runs leave human interaction disabled. From a real Windows Terminal session, run:

```powershell
.\build\test\GameWIPTests.exe --test-module=terminal --manual-ui
```

The opt-in suite requests human confirmation for Unicode rendering, colors and styles, cursor save/restore, alternate-screen restoration, interactive input, input-mode restoration, and cursor visibility. It skips with an explicit reason when the required streams are not real terminals.

## Test hooks

The validation preset enables internal hooks for approved build-tree targets. Their complete availability, reset, override, capture, and failure-injection contract is documented in @ref terminal_test_hooks.
