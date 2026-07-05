@page assert_failure_actions Assert failure actions

Interactive failures use `GameWIP::Debug::Assert::FailureAction`.

## Break

Break enters the platform debugger break path. If execution resumes, the program continues after the assertion handling path.

Use Break when the failure needs immediate debugger inspection.

If no debugger handles the break instruction, platform behavior can vary. Child-process tests cover this separately from the main runner.

## Abort

Abort terminates the process with `std::abort()`.

Use Abort when continuing would make the process state unsafe.

## Ignore Once

Ignore Once continues this single failure. The same macro call site can report again later.

Use Ignore Once to step past a known transient failure while still catching future occurrences.

## Always Ignore

Always Ignore suppresses future interactive failures from the same macro expansion site.

Important rules:

- Always Ignore is per call site.
- Always Ignore is not global.
- Always Ignore does not affect normal `ASSERT` / `VERIFY`.
- `ASSERT_INTERACTIVE` may skip condition evaluation after Always Ignore.
- `VERIFY_INTERACTIVE` still evaluates its condition.
- Per-call-site ignore state is thread-safe; concurrent failures may race only in which thread observes or sets the ignore first.

## Related pages

- @ref assert_interactive
- @ref assert_macro_behavior
