## Summary

-

## Linked Issues

- Closes #

## Validation

- Required CI: expected to run on the pull request.
- Change-specific local checks:
- Manual/optional checks (or why not needed):

## Merge Message

- Title: `area: imperative summary`
- Body: use the patch-note sections from `docs/contributing.md` when this PR becomes a non-trivial squash commit.

## Checklist

- [ ] Updated every owning helper, setup, CI, test, documentation, and
  version/reference surface when behavior, commands, tools, versions,
  workflows, options, or registries changed.
- [ ] Updated the owning implementation, workflow, or library documentation when behavior, automation, or validation changed.
- [ ] Verified generated documentation when public API comments, Doxygen pages, or library docs changed.
- [ ] Verified local Markdown links for maintained documentation when adding or changing links.
- [ ] Added or updated the owning correctness-test module for behavior changes where practical.
- [ ] Kept benchmark measurements separate from correctness tests and CI thresholds.
- [ ] Kept platform-specific code behind the internal backend boundary where applicable.
- [ ] Kept public API comments focused and moved extended usage guidance to Markdown docs where applicable.
- [ ] Followed the repository workflow standard in `docs/contributing.md`.
- [ ] Kept required checks, manual dispatches, and release-only validation distinct as documented in `docs/doxygen/repository_maintenance.md`.
