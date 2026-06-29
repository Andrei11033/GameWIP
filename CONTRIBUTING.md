# Contributing to GameWIP

GameWIP uses a small issue -> branch -> pull request workflow.

Use [docs/contributing.md](docs/contributing.md) for the full repository workflow standard, including issue titles, labels, branch names, pull request descriptions, validation notes, and squash commit messages.

Project status and linked pull request metadata are reconciled automatically. Static-analysis scope and repository-check commands are documented in [docs/doxygen/static_analysis.md](docs/doxygen/static_analysis.md).

Correctness tests live in discovered modules under `game/validation/tests`; benchmarks live under `game/validation/benchmarks`. Keep behavior checks out of benchmarks and performance thresholds out of correctness tests and CI.
