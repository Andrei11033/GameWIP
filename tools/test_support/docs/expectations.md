@page test_support_expectations TestSupport expectations and runners

## Runner and suite completion

`Runner` owns a shared sink and aggregates a suite only when `runSuite()` completes. Concurrent suite completion order is therefore not deterministic.

A suite callable may accept `Context&` or no arguments. If it throws, TestSupport records one failed check named `uncaught exception`, returns a normal `SuiteResult`, and allows later suites to run.

`Runner::result()` is the aggregate of completed suites. `Runner::exitCode()` returns one when at least one failure was recorded and zero otherwise, including skipped-only and empty runs.

## Context and counted outcomes

`Context::pass()`, `fail()`, and `skip()` each increment exactly one counter and write one categorized line. Informational methods do not alter counts.

`Summary::ok()` checks only the failure count. A skip is not a failure.

## Expectation families

Every expectation is an ordinary function call. Arguments are evaluated before TestSupport receives them; there is no assertion-macro laziness.

| API | Passing condition | Important caveat |
| --- | --- | --- |
| `expectTrue()` | value is `true` | Records one result. |
| `expectFalse()` | value is `false` | Records one result. |
| `expectEq()` | `expected == actual` | Comparison or failure formatting can throw. |
| `expectNe()` | `unexpected != actual` | Comparison or failure formatting can throw. |
| `expectNear()` | `abs(expected - actual) <= tolerance` | Negative tolerance fails. Floating-point non-finite values follow normal comparison rules. |
| `expectContains()` | substring occurs in text | Empty substring succeeds. |
| `expectFileContains()` | `fileContains()` returns true | Inherits the text-file helper's empty/open ambiguity. |
| `expectFileOccurrenceCount()` | observed non-overlapping count equals expected count | A zero count does not prove the file was readable. |

Each method records one pass or failure and returns the same outcome. Use the return value for dependent control flow; do not expect the helper to abort.

## Value formatting

`expectEq()` and `expectNe()` use stream insertion when the value type supports it. A non-streamable value is displayed as `<unprintable>`.

Comparison, `operator<<`, `std::ostringstream`, allocation, or report formatting can throw before a failure is fully recorded. Use a domain-specific check and explicit reason when formatting a value has side effects or requires a stronger diagnostic.

## File-expectation ambiguity

`readTextFile()` returns an empty string for an empty file and for open failure. `countFileOccurrences()` also returns zero for an empty search string, a missing/unreadable file, or no matches. Consequently:

- expecting zero occurrences can pass for a missing or unreadable file;
- `fileContains(path, "")` can succeed for an existing path whose read produced empty text.

Check `fileExists()` separately when existence is part of the contract. Use FileSystem or custom fixture code when open and read failures must be distinguished.

## Sections

`Section` copies its name but stores a reference to the context. The context must outlive the section.

Construction writes `begin section: <name>`. Destruction attempts to write one elapsed-time metric. Since the destructor is `noexcept`, formatting or reporting failures are suppressed; a lost section metric does not alter counts.

## Related pages

- @ref test_support_reports
- @ref test_support_files_environment
- @ref test_support_examples
