#!/usr/bin/env python3
"""Check repository-wide automation, policy-file, and community standards."""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
WORKFLOW_ROOT = ROOT / ".github" / "workflows"

REQUIRED_REPOSITORY_FILES = (
    ROOT / ".clang-format",
    ROOT / ".clang-tidy",
    ROOT / ".editorconfig",
    ROOT / ".gitattributes",
    ROOT / ".gitignore",
    ROOT / ".gitmodules",
    ROOT / ".vsconfig",
    ROOT / "CMakePresets.json",
    ROOT / "README.md",
    ROOT / "CONTRIBUTING.md",
    ROOT / "CODE_OF_CONDUCT.md",
    ROOT / "LICENSE",
    ROOT / "NOTICE",
    ROOT / "SECURITY.md",
    ROOT / ".github" / "CODEOWNERS",
    ROOT / ".github" / "dependabot.yml",
    ROOT / ".github" / "PULL_REQUEST_TEMPLATE.md",
    ROOT / ".github" / "ISSUE_TEMPLATE" / "config.yml",
    ROOT / ".github" / "ISSUE_TEMPLATE" / "bug_report.yml",
    ROOT / ".github" / "ISSUE_TEMPLATE" / "feature_request.yml",
    ROOT / ".github" / "ISSUE_TEMPLATE" / "task.yml",
    ROOT / ".github" / "workflows" / "docs.yml",
    ROOT / ".github" / "workflows" / "issue-area-labels.yml",
    ROOT / ".github" / "workflows" / "pr-standards.yml",
    ROOT / ".github" / "workflows" / "project-automation.yml",
    ROOT / ".github" / "workflows" / "release-preparation.yml",
    ROOT / ".github" / "workflows" / "validation.yml",
)

REQUIRED_CHECK_NAMES = {
    "pr-standards.yml": ("PR Standards", ("Check PR Standards",)),
    "validation.yml": (
        "Validation",
        (
            "Packages (CMake)",
            "Build and Test",
            "Repository Checks",
            "Coverage",
            "AddressSanitizer",
            "Docs Check",
        ),
    ),
}

ACTION_REFERENCE = re.compile(r"^\s*uses:\s*([^\s#]+)")
FULL_COMMIT_PIN = re.compile(r"^[^\s@]+@[0-9a-f]{40}$")
JOB_START = re.compile(r"^  ([A-Za-z0-9_-]+):\s*$")
JOB_TIMEOUT = re.compile(r"^    timeout-minutes:\s*[1-9][0-9]*\s*$")
CTEST_COMMAND = re.compile(r"^(?:run:\s*)?ctest(?:\s|$)")
CTEST_PRESET = re.compile(r"(?:^|\s)--preset(?:=|\s+)([A-Za-z0-9_.-]+)")
ISSUE_AREA_OPTIONS = re.compile(
    r"(?ms)^    id: area\s*$.*?^      options:\s*$\n((?:^        - [^\n]+\n?)+)"
)
ISSUE_AREA_MAPPING = re.compile(r'^\s+\["([^"]+)", "(area:[^"]+)"\],$', re.MULTILINE)

APACHE_LICENSE_MARKERS = (
    "Apache License\n                           Version 2.0, January 2004",
    "TERMS AND CONDITIONS FOR USE, REPRODUCTION, AND DISTRIBUTION",
    "END OF TERMS AND CONDITIONS",
)


def check_license_metadata(failures: list[str]) -> None:
    """Require one coherent first-party license and attribution boundary."""
    license_path = ROOT / "LICENSE"
    notice_path = ROOT / "NOTICE"
    readme_path = ROOT / "README.md"
    contributing_path = ROOT / "docs" / "contributing.md"
    decisions_path = ROOT / "docs" / "decisions.md"

    if license_path.is_file():
        license_text = license_path.read_text(encoding="utf-8")
        for marker in APACHE_LICENSE_MARKERS:
            if marker not in license_text:
                failures.append(
                    f"LICENSE: canonical Apache-2.0 marker is missing: {marker!r}"
                )

    if notice_path.is_file():
        notice_text = notice_path.read_text(encoding="utf-8")
        for marker in ("GameWIP", "Copyright 2026 Andrei11033", "external/"):
            if marker not in notice_text:
                failures.append(
                    f"NOTICE: required attribution marker is missing: {marker!r}"
                )

    required_references = {
        readme_path: ("Apache License 2.0", "(LICENSE)", "(NOTICE)", "external/"),
        contributing_path: ("Apache License 2.0", "section 5", "third-party"),
        decisions_path: ("Apache License 2.0", "accepted for public", "external/"),
    }
    for metadata_path, markers in required_references.items():
        if not metadata_path.is_file():
            continue
        text = metadata_path.read_text(encoding="utf-8")
        for marker in markers:
            if marker not in text:
                relative = metadata_path.relative_to(ROOT).as_posix()
                failures.append(
                    f"{relative}: license-policy marker is missing: {marker!r}"
                )


def workflow_jobs(lines: list[str]) -> list[tuple[str, list[str]]]:
    """Return top-level job blocks without requiring a YAML dependency."""
    try:
        jobs_index = lines.index("jobs:")
    except ValueError:
        return []

    jobs: list[tuple[str, list[str]]] = []
    current_name: str | None = None
    current_lines: list[str] = []

    for line in lines[jobs_index + 1 :]:
        match = JOB_START.match(line)
        if match:
            if current_name is not None:
                jobs.append((current_name, current_lines))
            current_name = match.group(1)
            current_lines = [line]
        elif current_name is not None:
            current_lines.append(line)

    if current_name is not None:
        jobs.append((current_name, current_lines))
    return jobs


def check_issue_area_mapping(failures: list[str]) -> None:
    """Require every non-manual issue-form area to map to one area label."""
    workflow_path = WORKFLOW_ROOT / "issue-area-labels.yml"
    if not workflow_path.is_file():
        return
    workflow_text = workflow_path.read_text(encoding="utf-8")
    mappings = dict(ISSUE_AREA_MAPPING.findall(workflow_text))
    offered: set[str] = set()

    for path in REQUIRED_REPOSITORY_FILES:
        if path.parent.name != "ISSUE_TEMPLATE" or path.name == "config.yml":
            continue
        match = ISSUE_AREA_OPTIONS.search(path.read_text(encoding="utf-8"))
        if match is None:
            failures.append(f"{path.relative_to(ROOT).as_posix()}: area dropdown was not found")
            continue
        options = {
            line.removeprefix("        - ").strip()
            for line in match.group(1).splitlines()
        }
        offered.update(options)
        for option in sorted(options - mappings.keys() - {"Other"}):
            failures.append(
                f"{path.relative_to(ROOT).as_posix()}: area option '{option}' has no label mapping"
            )

    for option in sorted(mappings.keys() - offered):
        failures.append(f".github/workflows/issue-area-labels.yml: unused area mapping '{option}'")


def fail_closed_ctest_presets(failures: list[str]) -> set[str]:
    """Return presets whose resolved no-tests action is error."""
    path = ROOT / "CMakePresets.json"
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        failures.append(f"CMakePresets.json: cannot resolve CTest policy: {error}")
        return set()

    presets = {preset["name"]: preset for preset in data.get("testPresets", []) if "name" in preset}
    resolved: dict[str, str | None] = {}

    def action_for(name: str, active: set[str]) -> str | None:
        if name in resolved:
            return resolved[name]
        if name in active or name not in presets:
            return None

        preset = presets[name]
        action = preset.get("execution", {}).get("noTestsAction")
        if action is None:
            parents = preset.get("inherits", [])
            if isinstance(parents, str):
                parents = [parents]
            for parent in parents:
                action = action_for(parent, active | {name})
                if action is not None:
                    break
        resolved[name] = action
        return action

    return {name for name in presets if action_for(name, set()) == "error"}


def check_workflow(path: Path, safe_ctest_presets: set[str]) -> list[str]:
    """Return actionable workflow-standard failures for one file."""
    relative = path.relative_to(ROOT).as_posix()
    text = path.read_text(encoding="utf-8")
    lines = text.splitlines()
    failures: list[str] = []

    if not re.search(r"(?m)^permissions:\s*$", text):
        failures.append(f"{relative}: declare explicit top-level token permissions")
    if re.search(r"(?m)^\s*permissions:\s*write-all\s*$", text):
        failures.append(f"{relative}: write-all token permissions are forbidden")

    required_checks = REQUIRED_CHECK_NAMES.get(path.name)
    if required_checks is not None:
        workflow_name, check_names = required_checks
        if not text.startswith(f"name: {workflow_name}\n"):
            failures.append(f"{relative}: workflow name must remain '{workflow_name}'")
        for check_name in check_names:
            if f"    name: {check_name}\n" not in text:
                failures.append(f"{relative}: protected check name must remain '{check_name}'")

    for line_number, line in enumerate(lines, start=1):
        match = ACTION_REFERENCE.match(line)
        if match:
            reference = match.group(1)
            if not reference.startswith("./") and not FULL_COMMIT_PIN.fullmatch(reference):
                failures.append(
                    f"{relative}:{line_number}: pin action '{reference}' to a full 40-character commit SHA"
                )

        command = line.strip()
        if CTEST_COMMAND.match(command) and "--no-tests=error" not in command:
            preset = CTEST_PRESET.search(command)
            if preset is None or preset.group(1) not in safe_ctest_presets:
                failures.append(
                    f"{relative}:{line_number}: CTest commands must fail when zero tests are discovered"
                )

    if re.search(r"(?m)^  pull_request_target:\s*$", text):
        if "github.event.pull_request.head" in text:
            failures.append(f"{relative}: pull_request_target must never reference untrusted head content")
        if "actions/checkout@" in text:
            if "ref: ${{ github.event.repository.default_branch }}" not in text:
                failures.append(f"{relative}: pull_request_target checkout must use the default branch")
            if "persist-credentials: false" not in text:
                failures.append(f"{relative}: pull_request_target checkout must not persist credentials")

    jobs = workflow_jobs(lines)
    if not jobs:
        failures.append(f"{relative}: define at least one job")
    for name, block in jobs:
        if not any(JOB_TIMEOUT.match(line) for line in block):
            failures.append(f"{relative}: job '{name}' must define timeout-minutes")

    return failures


def main() -> int:
    failures: list[str] = []

    for path in REQUIRED_REPOSITORY_FILES:
        if not path.is_file():
            failures.append(f"{path.relative_to(ROOT).as_posix()}: required repository file is missing")
        elif path.stat().st_size == 0:
            failures.append(f"{path.relative_to(ROOT).as_posix()}: required repository file is empty")

    check_license_metadata(failures)
    check_issue_area_mapping(failures)
    safe_ctest_presets = fail_closed_ctest_presets(failures)
    workflows = sorted((*WORKFLOW_ROOT.glob("*.yml"), *WORKFLOW_ROOT.glob("*.yaml")))
    if not workflows:
        failures.append(".github/workflows: no workflow files found")
    for workflow in workflows:
        failures.extend(check_workflow(workflow, safe_ctest_presets))

    if failures:
        print("Repository standards check failed:", file=sys.stderr)
        for failure in failures:
            print(f"- {failure}", file=sys.stderr)
        return 1

    print(
        f"Repository standards check passed for {len(workflows)} workflows "
        f"and {len(REQUIRED_REPOSITORY_FILES)} public repository files."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())