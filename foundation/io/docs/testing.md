@page io_testing Maintainer validation

@note This page documents validation coverage, not consumer API.

## Focused module

The `io` correctness module validates the public contract through the shared GameWIP validation runner. Run it with:

```powershell
.\build\test\GameWIPTests.exe --test-module=io --no-test-report
```

Use @ref project_testing for configure/build commands, reporting, CTest behavior, and validation authoring rules.

## Contract coverage

The focused suite covers:

- Every `ErrorCode` symbolic name, status defaults, and public status helpers.
- Reader and Writer construction, move-only behavior, default capabilities, flush validation, close, position, size, and seek behavior.
- MemoryReader source forms, temporary-source rejection, reads, overlapping destinations, end-of-stream, seeks, position, size, move state, and close
  state.
- MemoryWriter writes, self-aliasing input, flush and close state, checked capacity reservation and reuse, byte inspection, strict UTF-8 `copyText()`
  validation, ownership transfer, and move state.
- Known-size and unknown-size whole-stream reads.
- Current-position reads, empty streams, zero limits, exact limits, over-limit probes, custom scratch buffers, and invalid buffer arguments.
- Strict UTF-8 text reads across scalar/chunk boundaries, malformed and incomplete suffixes, valid-prefix preservation, and failure precedence.
- Partial progress, backend failures, impossible transfer counts, zero progress, premature end-of-stream, and capability-query failures.
- Whole-stream write retries, final-call progress, empty input, pre-write UTF-8 validation, and invalid writer behavior.
- Deterministic allocation, length, and unexpected-failure translation for memory-writer and whole-stream allocation points.
- Compile-time proof that public checked Reader and Writer operations are `noexcept`.

## Public and package validation

The repository validation also checks:

- independent compilation of every focused IO header and the `io/io.h` umbrella;
- installed consumption through both focused headers and the umbrella;
- A clean installed consumer using `find_package(IO ... EXACT CONFIG REQUIRED)` and `GameWIP::IO` without source-tree include paths; the IO package
  resolves its Unicode implementation dependency automatically.
- Project-wide compiler, sanitizer, coverage, and static-analysis workflows where enabled.

IO is static, so it has no shared-library export allowlist test. Package and compatibility policy are documented in @ref
project_library_compatibility.

Use @ref io_test_hooks for the source-tree-only deterministic failure API and reset protocol.

## Extension changes

When changing Reader, Writer, result, or helper behavior:

- Add focused coverage for the public contract being guaranteed.
- Include partial-progress and zero-progress cases where relevant.
- Verify both known-size and unknown-size paths when read-all behavior changes.
- Keep custom test adapters deterministic and free of operating-system dependencies unless the behavior belongs to a concrete backend library.

## Related pages

- @ref project_testing
- @ref io_public_api
- @ref io_reader_writer_contract
