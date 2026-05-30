# GameWIP Libraries

GameWIP uses small, focused C++20 libraries for foundation-level diagnostics:

- @ref logger: asynchronous runtime logging plus synchronous diagnostic reports.
- @ref assert: fatal assertions, recoverable checks, and interactive developer failure actions.

The generated documentation combines two kinds of content:

- **API reference** generated from public headers. These pages document contracts, parameters, return values, lifecycle behavior, blocking behavior, and performance notes.
- **Library guides** written as Markdown pages. These pages explain how to use each library without reading the implementation files.

## Build and validation references

- Project decisions: `docs/decisions.txt`
- Implementation checklist: `docs/implementation_checklist.txt`
- Testing checklist: `docs/testing_checklist.txt`
- Legacy Doxygen landing notes: @ref doxygen_notes
- External package smoke test: @ref package_smoke

## Documentation rule

Every public feature should have at least one of the following updated with it:

- a public Doxygen contract comment,
- a library guide page,
- the implementation checklist,
- the testing checklist.
