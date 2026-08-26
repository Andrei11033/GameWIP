@page assert_troubleshooting Troubleshooting

Assert behavior is determined by the chosen macro family, build-time options,
and whether an interactive handler is available. Use the symptom below to find
which part of that contract is active.

## The expression is not evaluated

The macro family is probably disabled. `ASSERT`, `CHECK`, `CHECK_ONCE`, and `ASSERT_INTERACTIVE` may skip the condition. Use `VERIFY`,
`VERIFY_INTERACTIVE`, or `ENSURE` if the expression must always run.

## The message expression does not run

`_MSG` arguments are diagnostic-only. They are not evaluated unless the failure is reported and diagnostics are enabled. Move required side effects
into the condition expression or surrounding code.

## A popup appears during automated tests

Automated tests should suppress real UI or use Assert test hooks. See @ref assert_testing and @ref assert_test_hooks.

## Popup settings did not change after adding a compile definition

`ASSERT_POPUP_ON_ASSERT` and `ASSERT_POPUP_ON_CHECK` are compiled into the Assert runtime. Defining them only on a consumer target does not change an
already-built shared runtime. Reconfigure/rebuild Assert with the intended runtime definitions or use popup suppression for tests.

## Always Ignore did not suppress another assert

Always Ignore is per macro expansion site. It is not a global ignore registry and does not affect normal `ASSERT`, `VERIFY`, `CHECK`, or another
interactive macro expansion.

## Break does not enter the debugger

Break requires a debugger that handles the platform break instruction. Non-interactive fatal failures break only when Assert detects an attached
debugger; otherwise they abort after reporting.

## Abort happens instead of Break

Without an attached debugger, the safe default interactive action is Abort. Tests can force debugger detection through @ref assert_test_hooks.

## The fallback dialog has fewer actions

The preferred Windows dialog can represent the full action set. The fallback MessageBox path cannot present a true four-action Always Ignore
interface.

## CHECK_ONCE reports more than expected

`CHECK_ONCE` is per macro expansion site. Two identical-looking calls on different lines are separate sites. Concurrent failures at the same site can
race on which thread performs the first report.

## The installed package cannot find Logger

A runtime-enabled Assert package depends on Logger. Install matching GameWIP packages and configure the consumer with a `CMAKE_PREFIX_PATH` that can
find the same exact project version.

## An installed consumer cannot include internal hooks

This is expected. `debug/assert/internal/assert_test_hooks.h` is source-tree-only and is not installed. Use public macros from `debug/assert/assert.h`
in consumer code.

## Related pages

- @ref assert_configuration
- @ref assert_macro_behavior
- @ref assert_abi
- @ref assert_interactive
