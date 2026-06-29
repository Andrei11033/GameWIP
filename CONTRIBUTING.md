# Contributing to GameWIP

GameWIP uses a small issue -> branch -> pull request workflow.

Use [docs/contributing.md](docs/contributing.md) for the full repository workflow standard, including issue titles, labels, branch names, pull request descriptions, validation notes, and squash commit messages.

Correctness tests live in discovered modules under `game/validation/tests`; benchmarks live under `game/validation/benchmarks`. Keep behavior checks out of benchmarks and performance thresholds out of correctness tests and CI.
