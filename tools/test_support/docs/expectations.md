@page test_support_expectations TestSupport expectations and runners

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

## Expectations

Expectations are normal test checks. They record a pass or failure and return a boolean result. They do not abort the process and they do not call the Assert macros.

Failure lines include the check name, reason, file, line, and function where practical.

Near comparisons reject negative tolerances. File expectations treat missing or unreadable files as failures rather than empty content.

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
