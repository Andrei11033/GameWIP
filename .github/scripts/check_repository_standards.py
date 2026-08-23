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
ISSUE_AREA_OPTIONS = re.compile(r"(?ms)^    id: area\s*$.*?^      options:\s*$\n((?:^        - [^\n]+\n?)+)")
ISSUE_AREA_MAPPING = re.compile(r"\[['\"]([^'\"]+)['\"],\s*['\"](area:[^'\"]+)['\"]\]")
SUPPORTED_PROVIDERS = {"msys2", "npm", "python", "powershellGallery", "githubRelease", "winget", "gitSubmodule", "external"}
OBSOLETE_LIVE_REFERENCES = (
    "build/tool-runs",
    "build/gamewip-temp",
    "build/unicode-data",
    ".gamewip-install-state.json",
    ".gamewip-setup.json",
    "GAMEWIP_ENABLE_TOOLS",
    "GAMEWIP_OPEN_TOOLS_AT_STARTUP",
    "check-documentation-standards.py",
    "check-markdown-links.py",
    "check-repository-standards.py",
    "C:\\MSYS2\\gamewip",
    "C:/MSYS2/gamewip",
)

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
                failures.append(f"LICENSE: canonical Apache-2.0 marker is missing: {marker!r}")

    if notice_path.is_file():
        notice_text = notice_path.read_text(encoding="utf-8")
        for marker in ("GameWIP", "Copyright 2026 Andrei11033", "external/"):
            if marker not in notice_text:
                failures.append(f"NOTICE: required attribution marker is missing: {marker!r}")

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
                failures.append(f"{relative}: license-policy marker is missing: {marker!r}")


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
    policy_path = ROOT / ".github" / "scripts" / "issue-area-labels.js"
    if not policy_path.is_file():
        failures.append(".github/scripts/issue-area-labels.js: extracted area policy is missing")
        return
    mappings = dict(ISSUE_AREA_MAPPING.findall(policy_path.read_text(encoding="utf-8")))
    offered: set[str] = set()

    for path in REQUIRED_REPOSITORY_FILES:
        if path.parent.name != "ISSUE_TEMPLATE" or path.name == "config.yml":
            continue
        match = ISSUE_AREA_OPTIONS.search(path.read_text(encoding="utf-8"))
        if match is None:
            failures.append(f"{path.relative_to(ROOT).as_posix()}: area dropdown was not found")
            continue
        options = {line.removeprefix("        - ").strip() for line in match.group(1).splitlines()}
        offered.update(options)
        for option in sorted(options - mappings.keys() - {"Other"}):
            failures.append(f"{path.relative_to(ROOT).as_posix()}: area option '{option}' has no label mapping")

    for option in sorted(mappings.keys() - offered):
        failures.append(f".github/scripts/issue-area-labels.js: unused area mapping '{option}'")


def read_json(relative: str, failures: list[str]) -> dict:
    """Read a required JSON authority while retaining all failures."""
    try:
        return json.loads((ROOT / relative).read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        failures.append(f"{relative}: cannot read JSON authority: {error}")
        return {}


def maintained_files() -> list[Path]:
    """Return first-party files without generated or disposable trees."""
    excluded = {".git", "build", "external"}
    return [path for path in ROOT.rglob("*") if path.is_file() and not excluded.intersection(path.relative_to(ROOT).parts)]


def check_registry_relationships(failures: list[str]) -> None:
    """Validate cross-file authorities and repository-facing relationships."""
    commands = read_json("scripts/config/commands.json", failures)
    project = read_json("scripts/config/project.json", failures)
    tools = read_json("scripts/config/project-tools.json", failures)
    setup = read_json("scripts/setup/config/setup.json", failures)
    helper = (ROOT / "scripts/GameWIP.ps1").read_text(encoding="utf-8")
    build_docs = "\n".join(
        (ROOT / relative).read_text(encoding="utf-8")
        for relative in ("docs/doxygen/build.md", "docs/doxygen/command_line_tools.md")
    )

    option_text = (ROOT / "cmake/GameWIPOptions.cmake").read_text(encoding="utf-8")
    options = set(re.findall(r"(?m)^option\((GAMEWIP_[A-Z0-9_]+)\s", option_text))
    options.update(re.findall(r"(?m)^set\((GAMEWIP_[A-Z0-9_]+)\s+[^\n]*CACHE\s", option_text))
    for option in sorted(options):
        if option not in build_docs:
            failures.append(f"cmake/GameWIPOptions.cmake: live build docs omit {option}")

    presets = read_json("CMakePresets.json", failures)
    visible = {
        preset["name"]
        for kind in ("configurePresets", "buildPresets", "testPresets")
        for preset in presets.get(kind, [])
        if not preset.get("hidden")
    }
    for preset in sorted(visible):
        if f"`{preset}`" not in build_docs:
            failures.append(f"CMakePresets.json: live build docs omit visible preset '{preset}'")

    workflow_text = "\n".join(path.read_text(encoding="utf-8") for path in WORKFLOW_ROOT.glob("*.yml"))
    workflow_defines = set(re.findall(r"-D(GAMEWIP_[A-Z0-9_]+)=", workflow_text))
    for variable in sorted(workflow_defines - options):
        failures.append(f".github/workflows: CMake -D variable '{variable}' is not a current option")

    for required_action in ("quality", "tools", "list", "doctor", "unicode", "workflow"):
        if f"'{required_action}'" not in helper:
            failures.append(f"scripts/GameWIP.ps1: helper action '{required_action}' is not dispatched")

    discovered_modules = {path.parent.name for path in (ROOT / "game/validation/tests").glob("*/CMakeLists.txt")}
    configured_modules = set(commands.get("modules", []))
    if discovered_modules != configured_modules:
        failures.append(
            "scripts/config/commands.json: modules differ from game/validation/tests directories "
            f"(configured={sorted(configured_modules)}, discovered={sorted(discovered_modules)})"
        )

    workflow_files = {path.name for path in WORKFLOW_ROOT.glob("*.yml")}
    for entry in commands.get("manualWorkflows", []):
        if entry.get("file") not in workflow_files:
            failures.append(
                f"scripts/config/commands.json: workflow '{entry.get('id')}' names missing file '{entry.get('file')}'"
            )

    for forbidden in ("wingetPackages", "msys2Packages", "cmakeVersionPattern"):
        if forbidden in setup:
            failures.append(f"scripts/setup/config/setup.json: duplicate tool authority '{forbidden}' is forbidden")

    tool_entries = tools.get("tools", [])
    tool_by_id = {entry.get("id"): entry for entry in tool_entries}
    if len(tool_by_id) != len(tool_entries):
        failures.append("scripts/config/project-tools.json: duplicate tool IDs")
    for tool_id in setup.get("bootstrapToolIds", []):
        if tool_id not in tool_by_id:
            failures.append(f"scripts/setup/config/setup.json: bootstrap tool '{tool_id}' is not registered")

    root_cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    cmake_match = re.search(r"(?m)^cmake_minimum_required\(VERSION\s+([0-9.]+)\)", root_cmake)
    cmake_tool = tool_by_id.get("cmake", {})
    if cmake_match is None:
        failures.append("CMakeLists.txt: cmake_minimum_required version was not found")
    elif cmake_tool.get("requiredVersion") != cmake_match.group(1):
        failures.append(
            "scripts/config/project-tools.json: CMake minimum must equal root cmake_minimum_required "
            f"({cmake_tool.get('requiredVersion')!r} != {cmake_match.group(1)!r})"
        )
    if "gamewip_cmake_version_pattern" in root_cmake or "newer on its release line" in root_cmake:
        failures.append("CMakeLists.txt: obsolete same-release-line CMake policy remains")

    provider_files = {path.stem.lower() for path in (ROOT / "scripts/lib/Providers").glob("*.ps1")}
    validation_text = (WORKFLOW_ROOT / "validation.yml").read_text(encoding="utf-8")
    tool_docs = (ROOT / "docs/doxygen/project_contracts.md").read_text(encoding="utf-8")
    for entry in tool_entries:
        tool_id = entry.get("id")
        provider_info = entry.get("provider", {})
        provider = provider_info.get("kind")
        if provider not in SUPPORTED_PROVIDERS:
            failures.append(f"scripts/config/project-tools.json: tool '{tool_id}' uses unsupported provider '{provider}'")
        elif provider.lower() not in provider_files:
            failures.append(f"scripts/lib/Providers: provider implementation '{provider}' is missing")

        if provider == "msys2":
            if provider_info.get("environment") not in {"common", "ucrt64", "clang64"}:
                failures.append(f"scripts/config/project-tools.json: MSYS2 tool '{tool_id}' lacks a valid environment")
            if not provider_info.get("package"):
                failures.append(f"scripts/config/project-tools.json: MSYS2 tool '{tool_id}' lacks a package")
            for dependency in provider_info.get("dependencies", []):
                if dependency.get("environment") not in {"common", "ucrt64", "clang64"}:
                    failures.append(
                        f"scripts/config/project-tools.json: MSYS2 dependency '{dependency.get('package')}' "
                        f"for '{tool_id}' lacks a valid environment"
                    )

        if provider == "npm":
            for dependency in provider_info.get("dependencies", []):
                if not dependency.get("version"):
                    failures.append(
                        f"scripts/config/project-tools.json: npm dependency '{dependency.get('package')}' "
                        f"for '{tool_id}' is not versioned"
                    )

        if provider == "githubRelease" and not provider_info.get("releaseTag"):
            failures.append(
                f"scripts/config/project-tools.json: GitHub release tool '{tool_id}' does not retain its actual upstream release tag"
            )

        version = entry.get("requiredVersion", "")
        for reference in entry.get("references", []):
            if not isinstance(reference, dict):
                failures.append(f"scripts/config/project-tools.json: tool '{tool_id}' has a non-object live reference")
                continue
            relative = reference.get("path", "")
            path = ROOT / relative
            if not path.is_file():
                failures.append(f"scripts/config/project-tools.json: tool '{tool_id}' references missing path '{relative}'")
                continue
            kind = reference.get("kind")
            if kind == "text":
                pattern = reference.get("pattern", "")
                if "{version}" not in pattern:
                    failures.append(
                        f"scripts/config/project-tools.json: text reference '{relative}' for '{tool_id}' lacks {{version}}"
                    )
                    continue
                expected = int(reference.get("expectedCount", 1))
                concrete = pattern.replace("{version}", version)
                count = path.read_text(encoding="utf-8").count(concrete)
                if count != expected:
                    failures.append(
                        f"{relative}: live version reference for '{tool_id}' expected {expected} exact match(es), found {count}"
                    )
            elif kind == "cmakeMinimum" and cmake_match is not None and version != cmake_match.group(1):
                failures.append(f"{relative}: CMake minimum reference for '{tool_id}' is stale")
            elif kind not in {"path", "text", "cmakeMinimum"}:
                failures.append(f"scripts/config/project-tools.json: tool '{tool_id}' uses unknown reference kind '{kind}'")

        if entry.get("versionPolicy") == "exact":
            if version not in tool_docs:
                failures.append(f"docs/doxygen/project_contracts.md: exact tool '{tool_id}' version '{version}' is stale")
            if tool_id not in validation_text:
                failures.append(
                    f".github/workflows/validation.yml: exact tool '{tool_id}' is not provisioned from registry metadata"
                )

    expected_storage = {
        "root": "build/gamewip",
        "cache": "build/gamewip/cache",
        "state": "build/gamewip/state",
        "temp": "build/gamewip/temp",
        "runs": "build/gamewip/runs",
    }
    if project.get("storage") != expected_storage:
        failures.append("scripts/config/project.json: repository-local storage must use build/gamewip/{cache,state,temp,runs}")

    registry_schemas = {
        "scripts/config/project.json": "scripts/schemas/project.schema.json",
        "scripts/config/commands.json": "scripts/schemas/commands.schema.json",
        "scripts/config/project-tools.json": "scripts/schemas/project-tools.schema.json",
        "scripts/setup/config/setup.json": "scripts/schemas/setup.schema.json",
        "scripts/setup/config/editors.json": "scripts/schemas/editors.schema.json",
    }
    for registry, schema in registry_schemas.items():
        if not (ROOT / registry).is_file() or not (ROOT / schema).is_file():
            failures.append(f"{registry}: required schema pairing '{schema}' is missing")

def check_live_paths_and_editor(failures: list[str]) -> None:
    """Reject migrated live names and invalid editor helper invocations."""
    for path in maintained_files():
        relative = path.relative_to(ROOT)
        if relative.as_posix() == ".github/scripts/check_repository_standards.py":
            continue
        if len(relative.parts) > 1 and relative.parts[:2] == ("docs", "releases"):
            continue
        if path.suffix.lower() not in {".md", ".ps1", ".py", ".js", ".json", ".yml", ".yaml", ".bat", ".cmake", ".txt"}:
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        for stale in OBSOLETE_LIVE_REFERENCES:
            if stale in text:
                failures.append(f"{relative.as_posix()}: obsolete live reference '{stale}'")

    tasks = read_json(".vscode/tasks.json", failures)
    labels = {task.get("label") for task in tasks.get("tasks", [])}
    for label in ("GameWIP: Quality Check", "GameWIP: Tools Status"):
        if label not in labels:
            failures.append(f".vscode/tasks.json: required helper task '{label}' is missing")

    validate_set = re.findall(r"\[ValidateSet\(([^\]]+)\)\]", (ROOT / "scripts/GameWIP.ps1").read_text(encoding="utf-8"))[0]
    helper_actions = set(validate_set.replace("'", "").replace(" ", "").split(","))
    for task in tasks.get("tasks", []):
        command = str(task.get("command", ""))
        args = task.get("args", [])
        if "gamewip.bat" in command and args and args[0] not in helper_actions:
            failures.append(f".vscode/tasks.json: task '{task.get('label')}' invokes unknown helper action '{args[0]}'")


def check_extracted_workflow_logic(failures: list[str]) -> None:
    """Keep substantial GitHub-script policy in tested source files."""
    required = {
        "pr-standards.yml": ("pr-standards.js", "pr-standards.test.js"),
        "issue-area-labels.yml": ("issue-area-labels.js", "issue-area-labels.test.js"),
    }
    for workflow, scripts in required.items():
        text = (WORKFLOW_ROOT / workflow).read_text(encoding="utf-8")
        for script in scripts:
            if not (ROOT / ".github/scripts" / script).is_file():
                failures.append(f".github/workflows/{workflow}: required extracted policy '{script}' is missing")
        if "new Map([" in text or "function " in text or "titlePattern" in text:
            failures.append(f".github/workflows/{workflow}: substantial inline program logic must remain extracted")


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
                failures.append(f"{relative}:{line_number}: pin action '{reference}' to a full 40-character commit SHA")

        command = line.strip()
        if CTEST_COMMAND.match(command) and "--no-tests=error" not in command:
            preset = CTEST_PRESET.search(command)
            if preset is None or preset.group(1) not in safe_ctest_presets:
                failures.append(f"{relative}:{line_number}: CTest commands must fail when zero tests are discovered")

    if re.search(r"(?m)^  pull_request_target:\s*$", text):
        if "github.event.pull_request.head" in text:
            failures.append(f"{relative}: pull_request_target must never reference untrusted head content")
        if "actions/checkout@" in text:
            if "ref: ${{ github.event.repository.default_branch }}" not in text:
                failures.append(f"{relative}: pull_request_target checkout must use the default branch")
            if "persist-credentials: false" not in text:
                failures.append(f"{relative}: pull_request_target checkout must not persist credentials")

    if relative == "pr-standards.yml":
        if "ref: ${{ github.event.pull_request.base.sha }}" not in text:
            failures.append(f"{relative}: PR policy code must be checked out from github.event.pull_request.base.sha")
        if "persist-credentials: false" not in text:
            failures.append(f"{relative}: trusted PR policy checkout must not persist credentials")

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
    check_registry_relationships(failures)
    check_live_paths_and_editor(failures)
    check_extracted_workflow_logic(failures)
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

    print(f"Repository standards check passed for {len(workflows)} workflows and {len(REQUIRED_REPOSITORY_FILES)} public repository files.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
