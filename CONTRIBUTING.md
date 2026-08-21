# Contributing to GameWIP

Thanks for helping improve GameWIP. Changes normally begin with a GitHub issue,
continue on a short-lived branch, and reach `master` through a reviewed pull
request with concrete validation notes. That keeps the history understandable
and the default branch ready to build.

Participation is governed by the [Contributor Code of Conduct](CODE_OF_CONDUCT.md).

Unless explicitly agreed otherwise, contributions accepted into GameWIP are
licensed under the repository's [Apache License 2.0](LICENSE). Submit only work
that you have the right to contribute; third-party material must retain its
applicable license and attribution.

Start with the [complete contributor workflow](docs/contributing.md). It walks
through issues, labels, branches, pull requests, validation evidence, project
automation, and squash commit messages in the order you will use them.

Maintainers should also use the
[repository maintenance policy](docs/doxygen/repository_maintenance.md) for
required checks, branch settings, manual workflow ownership, release gates, and
the public-repository audit checklist.

Use these references when the change reaches their area:

- [Extending the project](docs/doxygen/extending.md)
- [CMake infrastructure](docs/doxygen/cmake_infrastructure.md)
- [Documentation system](docs/doxygen/documentation.md)
- [Project decisions](docs/decisions.md)
- [Versioning policy](docs/versioning.md)

Correctness tests live in discovered modules under `game/validation/tests`;
benchmarks live under `game/validation/benchmarks`. Correctness tests prove
behavior. Benchmarks measure it, so performance thresholds do not belong in
correctness tests or CI gates.
