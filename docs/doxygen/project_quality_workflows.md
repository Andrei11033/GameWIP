@page project_quality_workflows Quality workflows and reports

Quality workflow pages explain how to run specialized verification tools, where they write results, and what evidence to record for quality-gate or
performance work.

Correctness validation is documented separately in @ref project_validation and @ref project_testing.

## Analysis and measurement workflows

- @subpage project_static_analysis — Run clang-tidy, formatting,
  documentation, link, workflow, and repository-consistency checks.
- @subpage project_coverage — Generate and interpret correctness-test coverage
  for the currently instrumented source set.
- @subpage project_profiling — Capture an instrumented runtime session with
  Tracy and interpret project-owned zones.
- @subpage project_benchmarking — Register, validate, measure, retain, and
  compare Google Benchmark scenarios.

## Related pages

- @ref project_manual
- @ref project_build
- @ref project_validation
