@page terminal_testing Maintainer validation

@note This page describes validation coverage and commands. Source-tree hook APIs are documented separately in @ref terminal_test_hooks.

## Automated coverage

Compile-only coverage checks every focused Terminal header and the umbrella from
a clean translation unit. Installed consumers exercise both include styles.

The automated suite also locks the backend-independent Text contract: malformed/incomplete redirected text is rejected before emission, `OutputBuffer`
preserves valid UTF-8 across failed mutations, invalid text segments fail before output, and byte segments continue to carry arbitrary binary data.

The Terminal validation module covers:

- public constants, value types, factories, deleted dangling-storage overloads, and header self-containment;
- stdout/stderr text, line, byte, formatted, segmented, and checked buffered writes;
- checked `OutputBuffer` line-ending configuration, mutation, partial-format rollback, size-limit translation, capacity reuse, flush retry, and
  `noexcept` header contracts;
- same-stream formatter reentry for global and Session `print()` / `println()`, nested global output from Session formatting, and checked
  same-operation Session close;
- complete-operation serialization, independent stdout/stderr state, and reusable scratch storage;
- capability observation, preparation, idempotence, and failure propagation;
- `Types::Style::Mode::Never`, `Auto`, and `Required`;
- redirected output, native line endings, UTF-8 conversion, and invalid-option rejection before normal emission;
- Win32 `INPUT_RECORD` key normalization including Unicode surrogate pairs, Ctrl/AltGr behavior, named/function/modifier keys, location, repeat
  splitting, and releases;
- stdin event/line/text/byte read contracts, code-point-safe limits, deadlines, polling, cancellation, EOF, resize dispatch, and incompatible-delivery
  rejection;
- managed echo after wrapping, Backspace/Delete and Left/Home edits across rows, simulated viewport scrolling, resize reflow, and redraw under the new
  width;
- managed Unicode line editing including grapheme-aware Backspace/Delete/navigation, Home/End, repeat handling, bounded paste insertion, partial
  outcomes, and no-echo operation;
- persistent Session open/close, AlreadyOpen, ResourceBusy ownership conflicts, exact native restoration, managed event-mode flags, output/input
  failed-close retry, move construction with pending output obligations, ordered destructor cleanup and failure bookkeeping release, and direct-read
  temporary restoration;
- Session-bound stdout/stderr capability/geometry queries, text/line/byte/segment/formatted output, control forwarding, reverse-order persistent
  output restoration, output progress during a deterministically blocked read, and close waiting for blocked input and formatting operations;
- cursor, alternate screen, title, bell, clear, scroll, size, position, and flush behavior;
- scope setup, nesting, move behavior, explicit restoration, write failure, post-emission flush failure, ownership preservation, and non-duplicating
  retry;
- Logger integration through the shared Terminal runtime;
- internal backend boundaries and forced failure paths.

## Project validation layers

Terminal participates in:

- the focused Terminal validation module;
- public-header self-containment validation;
- installed-package consumer validation through `GameWIP::Terminal`;
- Logger integration validation;
- child-process validation for reentrant formatting paths;
- opt-in manual real-terminal validation, including a wrapped line edited near the viewport bottom while the terminal is resized.

GameWIP owns module selection, CTest registration, reports, and sanitizer workflows. See @ref project_testing and @ref project_validation.

## Diagnostic benchmarks

The `terminal` benchmark module covers retained-capacity checked `OutputBuffer` append, line batching, and formatting. These benchmarks are intended
to catch accidental extra allocation/copying in the caller-owned buffering path; they do not benchmark console-driver latency.

## Real-terminal validation

Automated runs leave human interaction disabled. From a real Windows Terminal session, run:

```powershell
.\build\test\GameWIPTests.exe --test-module=terminal --manual-tests
```

The opt-in suite requests human confirmation for Unicode rendering, colors and styles, cursor save/restore, alternate-screen restoration, managed line
input, native structured Left-Arrow delivery, managed session restoration, and cursor visibility. It skips with an explicit reason when the required
streams are not real terminals.

## Test hooks

The validation preset enables internal hooks for approved build-tree targets. Their complete availability, reset, override, capture, and
failure-injection contract is documented in @ref terminal_test_hooks.
