@page test_support_expectations TestSupport expectations and runners

This page explains `Runner`, `Context`, `Section`, and the expectation API.

## Runner

`GameWIP::TestSupport::Runner` owns one shared report sink and aggregates named suites.

Use it when a test executable runs more than one suite or when you want one final exit code:

```cpp
GameWIP::TestSupport::Types::ReportOptions options;
GameWIP::TestSupport::Runner runner(options);

runner.runSuite("Math", [](GameWIP::TestSupport::Context& context)
{
    context.expectEq("one plus one", 2, 1 + 1);
});

return runner.exitCode();
```

If a suite throws, the runner records a failure for that suite instead of letting the whole test executable skip later suites.

## Context

`Context` is the suite-level recorder. It writes categorized lines and owns the suite summary.

Recording calls:

- `info(message)` writes `[INFO]`.
- `manual(instruction)` writes `[MANUAL]`.
- `metric(message)` writes `[METRIC]`.
- `stress(message)` writes `[STRESS]`.
- `summary(message)` writes `[SUMMARY]`.
- `pass(name)` increments pass count.
- `fail(name, reason, location)` increments fail count and includes source-location details.
- `skip(name, reason)` increments skip count.

The non-result categories do not change pass/fail/skip counts.

## Expectations

Expectations are normal test checks. They record a pass or failure and return a boolean result. They do not abort the process and they do not call the GameWIP Assert macros.

| API | Passes when |
| --- | --- |
| `expectTrue(name, value)` | `value` is true. |
| `expectFalse(name, value)` | `value` is false. |
| `expectEq(name, expected, actual)` | `expected == actual`. |
| `expectNe(name, unexpected, actual)` | `unexpected != actual`. |
| `expectNear(name, expected, actual, tolerance)` | Absolute difference is within non-negative tolerance. |
| `expectContains(name, text, substring)` | `substring` occurs in `text`. |
| `expectFileContains(name, path, substring)` | The file exists/readable and contains `substring`. |
| `expectFileOccurrenceCount(name, path, text, count)` | The file contains exactly `count` non-overlapping occurrences. |

Failure lines include the check name, reason, file, line, and function where practical.

## Sections

`Section` groups large scenarios and reports timing through RAII:

```cpp
{
    GameWIP::TestSupport::Section section(context, "load scenario");
    runLoadScenario();
}
```

Sections are for readability and diagnostics. They do not change result counts by themselves.

## Value formatting

`expectEq` and `expectNe` format values through stream insertion when available. Non-streamable values are reported as `<unprintable>`. Prefer explicit custom expectations when a type needs domain-specific failure messages.
