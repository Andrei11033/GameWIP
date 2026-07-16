# Contributing to GameWIP

GameWIP uses GitHub issues, short-lived branches, pull requests, required validation notes, and squash merges to keep `master` readable and releasable.

Read [docs/contributing.md](docs/contributing.md) for the full contributor workflow, including issue titles, labels, branch names, pull request metadata, validation evidence, project automation, and squash commit messages.

For implementation and review standards, also read:

- [Extending the project](docs/doxygen/extending.md)
- [CMake infrastructure](docs/doxygen/cmake_infrastructure.md)
- [Documentation system](docs/doxygen/documentation.md)
- [Project decisions](docs/decisions.md)
- [Versioning policy](docs/versioning.md)

Correctness tests live in discovered modules under `game/validation/tests`; benchmarks live under `game/validation/benchmarks`. Keep behavior checks out of benchmarks and performance thresholds out of correctness tests and CI.
