@page test_support_expectations Expectations and runners

Contexts record individual checks, while runners collect completed suites.
This separation lets tests report useful failures and continue through later
suites without hiding infrastructure errors.

## Runner and suite completion

`Runner` owns a shared sink and aggregates a suite after its callable returns or its exception is recorded. Concurrent suite completion and report
order are not deterministic.

A suite callable may accept `Context&` or no arguments. If it throws, TestSupport records one failed check named `uncaught exception`, returns a
normal `SuiteResult`, and allows later suites to run.

That exception conversion covers the suite callable. Context construction, failure formatting, result construction, and report output can still
throw. A report-output exception can escape after aggregate counts have been updated; it does not roll those counts back.

`Runner::result()` is the aggregate of completed suites. `Runner::exitCode()` returns one when at least one failure was recorded and zero otherwise,
including skipped-only and empty runs.

`Runner::exitCode()` remains assertion-only. Inspect `Runner::finalize()` or
`reportStatus()` separately when report-file delivery matters; a `Failed` sink
is an infrastructure/artifact result, not an assertion failure.

## Context and counted outcomes

`Context::pass()`, `fail()`, and `skip()` each increment exactly one counter and write one categorized line. Informational methods do not alter
counts.

Context recording, runner aggregation, and report writes each use internal synchronization. A count snapshot is coherent, but count updates and
their report lines are separate operations. Keep contexts and runners alive until all calls and suite workers have finished; caller-owned values,
formatters, and suite state require their own synchronization. A suite callable must join workers that borrow its Context before returning.

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
| `expectFileContains()` | `fileContains()` returns successful status and true | A failed file operation records a failed expectation with status details. |
| `expectFileOccurrenceCount()` | successful status and observed non-overlapping count equals expected count | Successful zero distinguishes no matches from read failure. |

Each method records one pass or failure and returns the same outcome. Use the return value for dependent control flow; do not expect the helper to
abort.

## Value formatting

`expectEq()` and `expectNe()` use stream insertion when the value type supports it. Otherwise, an enum is displayed as its numeric underlying value;
other non-streamable values are displayed as `<unprintable>`. A custom stream formatter takes precedence over enum formatting.
Boolean values are displayed as `true` or `false`.

Comparison, `operator<<`, `std::ostringstream`, allocation, or report formatting can throw before a failure is fully recorded. Use a domain-specific
check and explicit reason when formatting a value has side effects or requires a stronger diagnostic.

## File-expectation status

File expectations require successful infrastructure status before considering the domain value. `expectFileContains(path, "")` passes for any
successfully read file, including an empty file, but fails for missing or unreadable input. A zero occurrence expectation likewise requires a
successful read.

## Sections

`Section` copies its name but stores a reference to the context. The context must outlive the section.

Construction writes `begin section: <name>`. Destruction attempts to write one elapsed-time metric. Since the destructor is `noexcept`, formatting or
reporting failures are suppressed; a lost section metric does not alter counts.

## Related pages

- @ref test_support_reports
- @ref test_support_files_environment
- @ref test_support_examples
