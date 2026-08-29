#!/usr/bin/env python3
"""Reject superseded helper architecture, CLI grammar, scratch artifacts, and bypasses."""

from __future__ import annotations

import json
import re
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HISTORICAL_PREFIXES = ("docs/releases/",)
ENGINE_AUDIT_EXCLUSIONS = (
    "engine/input/",
    "engine/action/",
    "engine/desktop/",
    "engine/window_manager/",
    "engine/window-manager/",
)
FORBIDDEN_PATHS = {
    "scripts/lib/Native.ps1",
    "scripts/lib/ToolRuns.ps1",
    "scripts/lib/Analysis.ps1",
    ".github/scripts/ProjectHelper.Tests.debug.ps1",
    ".github/scripts/ProjectHelper.Tests.debug2.ps1",
    "test-debug.ps1",
    "test-debug2.ps1",
}
RETIRED_SELECTORS = (
    "-QualityAction",
    "-ToolsAction",
    "-WorkflowAction",
    "-UnicodeAction",
    "-GitAction",
    "-FormatAction",
    "-BenchmarkAction",
)
LEGACY_SELECTION = ("-Preset", "-Module", "-ProjectCommand", "-Bundle")
RETIRED_OPERATION_GLOBALS = ("$Script:OperationTemp", "$Script:OperationRun")
EXTERNAL_LITERAL = re.compile(
    r"&\s+(git|gh|python3?|npm|winget|cmake|ctest|clang-format|ruff|prettier|eslint|actionlint|yamllint|gersemi|markdownlint-cli2|objdump|pacman|where\.exe|chmod)\b",
    re.I,
)


def maintained_worktree_paths() -> list[str]:
    result = subprocess.run(
        ["git", "ls-files", "--cached", "--others", "--exclude-standard", "-z"],
        cwd=ROOT,
        check=True,
        capture_output=True,
    )
    return [item.decode("utf-8") for item in result.stdout.split(b"\0") if item and (ROOT / item.decode("utf-8")).is_file()]


def main() -> int:
    paths = maintained_worktree_paths()
    path_set = set(paths)
    failures: list[str] = []
    for forbidden in sorted(FORBIDDEN_PATHS & path_set):
        failures.append(f"forbidden superseded/scratch path is tracked: {forbidden}")
    for path in paths:
        normalized = path.replace("\\", "/")
        if normalized.startswith(HISTORICAL_PREFIXES) or normalized.startswith(ENGINE_AUDIT_EXCLUSIONS):
            continue
        if normalized.startswith("scripts/") and normalized.endswith(".psm1"):
            failures.append(f"PowerShell module helper architecture is not allowed: {normalized}")
        file = ROOT / path
        if not file.is_file():
            continue
        try:
            text = file.read_text(encoding="utf-8-sig")
        except (UnicodeDecodeError, OSError):
            continue
        if normalized == ".github/scripts/check_helper_standardization.py":
            continue
        cli_contract_surface = normalized.startswith(("scripts/", ".github/", ".vscode/", "docs/")) or normalized in {
            "README.md",
            "docs/doxygen/command_line_tools.md",
            "docs/doxygen/environment_setup.md",
            "scripts/setup/README.md",
        }
        if cli_contract_surface:
            for selector in RETIRED_SELECTORS:
                if selector in text:
                    failures.append(f"{normalized}: retired CLI selector remains: {selector}")
            for selector in LEGACY_SELECTION:
                if re.search(rf"(?im)\bgamewip(?:\.bat)?\b[^\r\n]*\s{re.escape(selector)}\b", text):
                    failures.append(f"{normalized}: retired GameWIP selection option remains: {selector}")
        if re.search(r"(?im)^\s*\.\s+[^\r\n]*GameWIP\.ps1\b", text):
            failures.append(f"{normalized}: executable GameWIP.ps1 is dot-sourced as a library")
        if normalized != "scripts/lib/Console.ps1" and "[Console]::ReadKey" in text:
            failures.append(f"{normalized}: direct console key input bypasses Console.ps1")
        if normalized.startswith("scripts/"):
            for match in EXTERNAL_LITERAL.finditer(text):
                failures.append(f"{normalized}: direct external invocation bypasses Process.ps1: {match.group(0)}")
            for variable in RETIRED_OPERATION_GLOBALS:
                if variable in text:
                    failures.append(f"{normalized}: retired operation global remains: {variable}")
            if re.search(r"\b(?:if|elseif|while)\s*\(\s*Test-GameWip[\w-]+\s+-(?:and|or)\b", text):
                failures.append(f"{normalized}: helper predicate must be parenthesized before a Boolean operator")
        if re.search(r"(?i)(?:^|[\\/ ])gamewip(?:\.bat)?\s+analysis(?:\s|$)", text):
            failures.append(f"{normalized}: retired 'analysis' alias remains")
        if normalized.endswith(".json"):
            try:
                document = json.loads(text)
            except json.JSONDecodeError:
                document = None
            if isinstance(document, dict) and "schemaVersion" in document and document["schemaVersion"] != 1:
                failures.append(f"{normalized}: schemaVersion must remain 1 on this branch")

    # Canonical path-casing check: configured repository references must match Git casing exactly.
    canonical = {path.casefold(): path for path in paths}
    registry = ROOT / "scripts/config/project-tools.json"
    if registry.exists():
        data = json.loads(registry.read_text(encoding="utf-8"))
        for tool in data["tools"]:
            for reference in tool.get("references", []):
                value = reference["path"].replace("\\", "/")
                actual = canonical.get(value.casefold())
                if actual is not None and actual != value:
                    failures.append(f"project-tools reference casing drift: {value!r} should be {actual!r}")

    if failures:
        print("Helper/repository standardization checks failed:")
        for failure in sorted(set(failures)):
            print(f"  - {failure}")
        return 1
    print("Helper/repository standardization checks passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
