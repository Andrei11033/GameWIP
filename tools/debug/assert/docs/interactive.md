@page assert_interactive Interactive asserts

Interactive assertions are separate from normal `ASSERT` and `VERIFY`.

They are intended for developer/debug workflows where the developer may choose what happens after a failure.

Available actions:

- **Break**: trigger the debugger break path and continue if the debugger resumes.
- **Abort**: terminate the process.
- **Ignore Once**: continue this time only.
- **Always Ignore**: suppress future failures from the same macro call site.

`AlwaysIgnore` is local to the macro expansion site. It is not a global ignore list.

## Dialog behavior

On Windows, Assert prefers a dialog that presents the full action set. If that dialog is unavailable, Assert uses a fallback dialog with a reduced action mapping.

When a debugger is attached, the default action favors Break. Without a debugger, the safe default is Abort.

## Automation

Unattended tests must not enable real dialogs. Maintainers can validate interactive behavior deterministically through the source-tree interfaces described in @ref assert_testing and @ref assert_test_hooks.
