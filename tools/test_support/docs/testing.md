@page test_support_testing Maintainer validation

This page maps TestSupport's public and source-tree-only behavior to the tests
that prove it. Consumer usage belongs in the quick start and focused guides.

## Correctness coverage

TestSupport correctness validation covers reporting, expectations, strict UTF-8 files, process-global guards, child-process outcomes/capture, manual prompts, deterministic failure hooks, and stress primitives.

Text-file tests include malformed and incomplete UTF-8, valid-prefix preservation, and validation-before-destructive-write behavior. Child capture tests treat stdout/stderr as arbitrary bytes, including truncation and zero-retention cases.

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

Installed-consumer validation exercises the same focused headers through the installed package and proves that the package discovers its exact Unicode dependency without exposing source-tree test-hook definitions.

## Suite organization

The TestSupport correctness suite is one logical module and one translation unit. Private `.inl` fragments group reporting/manual/runner, files/text, environment, child-process, and stress behavior while sharing TU-local child protocols and fixtures. The fragments are test organization only and are not reusable support headers.

Test hooks are enabled through `TEST_SUPPORT_ENABLE_TEST_HOOKS` and the source-tree-only `TEST_SUPPORT_INTERNAL_TEST_HOOKS` definition.

## Related pages

@ref project_testing
@ref test_support_test_hooks
