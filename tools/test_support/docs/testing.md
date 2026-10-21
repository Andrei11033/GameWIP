@page test_support_testing Testing

This page maps TestSupport's public and source-tree-only behavior to the tests
that prove it. Consumer usage belongs in the quick start and focused guides.

## Correctness coverage

TestSupport correctness validation covers reporting, expectations, strict UTF-8 files, process-global guards, child-process outcomes/capture, manual
prompts, and stress primitives.

Text-file tests include malformed and incomplete UTF-8, valid-prefix preservation, and validation-before-destructive-write behavior. Child capture
tests treat stdout/stderr as arbitrary bytes, including truncation and zero-retention cases.

StartGate checks observe workers approaching a closed gate, verify no worker passes before opening, then check repeated opening and future waiters.
A 30-second child-process timeout bounds synchronization regressions; the closed-gate negative observation lasts 100 ms. Worker tests also cover
zero workers, indexed and no-argument callables, independent callable copies, and exception propagation after peer joins.

Enum expectation failures show underlying numeric values instead of an unprintable placeholder, including narrow unsigned and negative signed values.
An existing stream formatter still takes precedence. Nested report fixtures verify the resulting diagnostics and failure counts.

## Public-header and package validation

The public-header validation compiles each supported entry header independently:

```text
test_support/types.h
test_support/reporting.h
test_support/files.h
test_support/process.h
test_support/stress.h
test_support/test_support.h
```

Installed-consumer validation exercises the same focused headers through the installed package and proves that the package discovers its exact Unicode
dependency without exposing source-tree validation definitions.

## Suite organization

The TestSupport correctness suite is one logical module and one translation unit. Scenario groups cover reporting/manual/runner, files/text,
environment, child-process, and stress behavior while sharing TU-local child protocols and fixtures. These groups are test organization only and are
not reusable support headers.

## Related pages

@ref project_testing
