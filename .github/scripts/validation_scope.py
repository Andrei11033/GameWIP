"""Select the smallest safe validation scope for one GitHub event."""

from __future__ import annotations

import argparse
import json
import subprocess
from pathlib import Path

CPP_SUFFIXES = {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx", ".inl"}
HEADER_SUFFIXES = {".h", ".hh", ".hpp", ".hxx"}
LIBRARY_ROOTS = ("foundation/", "tools/", "engine/", "game/")
PACKAGE_ROOTS = ("foundation/", "tools/", "engine/")
DOC_SUFFIXES = {".md", ".markdown", ".dox"}
SCRIPT_SUFFIXES = {".js", ".mjs", ".cjs", ".py", ".ps1", ".psm1", ".psd1"}
FORMAT_SUFFIXES = {".json", ".jsonc", ".yml", ".yaml", ".css", ".toml"}
BUILD_GRAPH_FILES = {
    "CMakeLists.txt",
    "CMakePresets.json",
    ".github/scripts/validation_scope.py",
    ".github/workflows/validation.yml",
    "scripts/config/build-targets.json",
    "scripts/config/commands.json",
    "scripts/config/dependencies.json",
    "scripts/config/project-tools.json",
    ".clang-format",
    ".clang-tidy",
    ".editorconfig",
}
CMAKE_SUFFIXES = {".cmake"}
SAFE_TEXT_FILES = {"LICENSE", "NOTICE", ".editorconfig", ".gitignore"}
COVERAGE_FILES = {
    "cmake/GameWIPCoverage.cmake",
    "game/validation/CMakeLists.txt",
    "game/validation/tests/CMakeLists.txt",
}


def run_git(*arguments: str) -> list[str]:
    result = subprocess.run(
        ["git", *arguments],
        check=True,
        capture_output=True,
    )
    return [path.decode("utf-8", errors="surrogateescape") for path in result.stdout.split(b"\0") if path]


def changed_files(event: str, base: str, head: str, before: str, ref: str, force_full: bool) -> tuple[list[str], bool, str]:
    try:
        if event == "pull_request":
            left, right = base, head
        elif event == "push":
            if before and set(before) != {"0"}:
                left = before
            else:
                left = (
                    subprocess.run(
                        ["git", "hash-object", "-t", "tree", "--stdin"],
                        input=b"",
                        check=True,
                        capture_output=True,
                        text=False,
                    )
                    .stdout.decode("ascii")
                    .strip()
                )
            right = head
        elif event == "workflow_dispatch":
            if force_full:
                return (
                    run_git("ls-files", "--cached", "--others", "--exclude-standard", "-z"),
                    True,
                    "manual full-validation override",
                )
            if ref == "master":
                try:
                    left = subprocess.run(
                        ["git", "rev-parse", "HEAD^"],
                        check=True,
                        capture_output=True,
                        text=True,
                    ).stdout.strip()
                except subprocess.CalledProcessError:
                    return (
                        run_git(
                            "ls-files",
                            "--cached",
                            "--others",
                            "--exclude-standard",
                            "-z",
                        ),
                        True,
                        "no parent commit",
                    )
                right = head or "HEAD"
            else:
                left, right = "origin/master...HEAD", ""
        else:
            return (
                run_git("ls-files", "--cached", "--others", "--exclude-standard", "-z"),
                True,
                "unknown GitHub event",
            )

        if right:
            files = run_git("diff", "--name-only", "--no-renames", "-z", left, right)
        else:
            files = run_git("diff", "--name-only", "--no-renames", "-z", left)
        return sorted(set(files)), False, ""
    except (OSError, subprocess.CalledProcessError):
        return (
            run_git("ls-files", "--cached", "--others", "--exclude-standard", "-z"),
            True,
            "could not compare event revisions",
        )


def classify(files: list[str], forced_full: bool, full_reason: str) -> dict[str, object]:
    normalized = [path.replace("\\", "/") for path in files]
    full_reasons = [
        path
        for path in normalized
        if path in BUILD_GRAPH_FILES
        or path.startswith(("cmake/", "config/quality/"))
        or (path.endswith("/CMakeLists.txt") and path.startswith(LIBRARY_ROOTS))
    ]

    cpp_files = [path for path in normalized if path.startswith(LIBRARY_ROOTS) and Path(path).suffix.lower() in CPP_SUFFIXES]
    docs_files = [path for path in normalized if Path(path).suffix.lower() in DOC_SUFFIXES or path.startswith("docs/") or "/docs/" in path]
    package_files = [path for path in normalized if path.startswith(PACKAGE_ROOTS) and Path(path).suffix.lower() in HEADER_SUFFIXES]
    documented_headers = [path for path in normalized if path.startswith(LIBRARY_ROOTS) and Path(path).suffix.lower() in HEADER_SUFFIXES]
    test_infrastructure = any(path in COVERAGE_FILES for path in normalized) or any(
        path.startswith("game/validation/tests/internal/")
        or path
        in {
            "game/validation/tests/registry.cpp",
            "game/validation/tests/registry.h",
            "game/validation/tests/runner.cpp",
            "game/validation/tests/runner.h",
            "game/validation/tests/standalone_main.cpp",
        }
        for path in normalized
    )
    benchmark_changes = any(path.startswith("game/validation/benchmarks/") for path in normalized)

    needs_python = any(Path(path).suffix.lower() == ".py" for path in normalized)
    needs_powershell = any(Path(path).suffix.lower() in {".ps1", ".psm1", ".psd1"} for path in normalized)
    needs_javascript = any(Path(path).suffix.lower() in {".js", ".mjs", ".cjs"} for path in normalized)
    needs_json = any(Path(path).suffix.lower() in {".json", ".jsonc"} or path.endswith(".code-workspace") for path in normalized)
    needs_yaml = any(Path(path).suffix.lower() in {".yml", ".yaml"} for path in normalized)
    needs_markdown = any(Path(path).suffix.lower() in DOC_SUFFIXES for path in normalized)
    needs_cmake_quality = any(Path(path).suffix.lower() in CMAKE_SUFFIXES or Path(path).name == "CMakeLists.txt" for path in normalized)
    needs_prettier = any(
        Path(path).suffix.lower() in {".js", ".mjs", ".cjs", ".json", ".jsonc", ".yml", ".yaml", ".css"}
        or Path(path).name in {".vsconfig", "GameWIP.code-workspace"}
        for path in normalized
    )
    needs_actionlint = any(path.startswith(".github/workflows/") and Path(path).suffix.lower() in {".yml", ".yaml"} for path in normalized)
    needs_symlink_tests = any(path.startswith(("foundation/filesystem/", "game/validation/tests/filesystem/")) for path in normalized)
    needs_unicode_data = any(path.startswith(("foundation/unicode/", "game/validation/tests/unicode/")) for path in normalized)

    known_paths = set(BUILD_GRAPH_FILES | COVERAGE_FILES | SAFE_TEXT_FILES)
    unknown_files = []
    for path in normalized:
        suffix = Path(path).suffix.lower()
        known = (
            path in known_paths
            or path.startswith(
                (
                    "cmake/",
                    "config/quality/",
                    "docs/",
                    "external/",
                    "build/",
                    "install/",
                )
            )
            or (path.startswith(LIBRARY_ROOTS) and suffix in CPP_SUFFIXES | SCRIPT_SUFFIXES | FORMAT_SUFFIXES | DOC_SUFFIXES | CMAKE_SUFFIXES)
            or suffix in SCRIPT_SUFFIXES | FORMAT_SUFFIXES | DOC_SUFFIXES | CMAKE_SUFFIXES
            or Path(path).name == "CMakeLists.txt"
        )
        if not known:
            unknown_files.append(path)

    full_reasons.extend(unknown_files)
    full = forced_full or bool(full_reasons)

    needs_cpp = full or bool(cpp_files)
    needs_docs = full or bool(docs_files) or bool(documented_headers)
    needs_packages = full or bool(package_files)
    needs_coverage = full or test_infrastructure
    needs_benchmarks = full or benchmark_changes
    needs_repository_checks = full or any(
        path not in SAFE_TEXT_FILES and not (path.startswith(LIBRARY_ROOTS) and Path(path).suffix.lower() in CPP_SUFFIXES) for path in normalized
    )
    needs_dependencies = needs_cpp or needs_docs or needs_packages or needs_coverage or needs_benchmarks

    if full:
        scope = "full"
    elif needs_cpp:
        scope = "code"
    elif needs_docs:
        scope = "docs"
    elif needs_repository_checks:
        scope = "repository"
    else:
        scope = "empty"

    reasons = [full_reason] if full_reason else []
    reasons.extend(full_reasons)

    return {
        "changed_files": normalized,
        "scope": scope,
        "full_validation": full,
        "full_reasons": sorted(set(reasons)),
        "needs_cpp": needs_cpp,
        "needs_docs": needs_docs,
        "needs_packages": needs_packages,
        "needs_coverage": needs_coverage,
        "needs_benchmarks": needs_benchmarks,
        "needs_repository_checks": needs_repository_checks,
        "needs_dependencies": needs_dependencies,
        "needs_python": full or needs_python,
        "needs_powershell": full or needs_powershell,
        "needs_javascript": full or needs_javascript,
        "needs_json": full or needs_json,
        "needs_yaml": full or needs_yaml,
        "needs_markdown": full or needs_markdown,
        "needs_cmake_quality": full or needs_cmake_quality,
        "needs_prettier": full or needs_prettier,
        "needs_actionlint": full or needs_actionlint,
        "needs_symlink_tests": full or needs_symlink_tests,
        "needs_unicode_data": full or needs_unicode_data,
        "needs_helper_tests": full or any(path.startswith(("scripts/", ".github/scripts/")) for path in normalized),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--event", required=True)
    parser.add_argument("--base", default="")
    parser.add_argument("--head", default="")
    parser.add_argument("--before", default="")
    parser.add_argument("--ref", default="")
    parser.add_argument("--full", action="store_true")
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()

    files, comparison_failed, reason = changed_files(args.event, args.base, args.head, args.before, args.ref, args.full)
    scope = classify(files, args.full or comparison_failed, reason)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(scope, ensure_ascii=True, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(scope, ensure_ascii=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
