#!/usr/bin/env python3
"""Validate repository documentation ownership and navigation contracts."""

from __future__ import annotations

import json
import re
import sys
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PAGE_PATTERN = re.compile(r"^@page\s+(\S+)\s+(.+)$", re.MULTILINE)
SUBPAGE_PATTERN = re.compile(r"@subpage\s+(\S+)")
FENCE_PATTERN = re.escape(chr(96) * 3)
FENCED_CODE_PATTERN = re.compile(
    rf"^{FENCE_PATTERN}[^\n]*\n.*?^{FENCE_PATTERN}\s*$",
    re.MULTILINE | re.DOTALL,
)
LIBRARY_DOCS = (
    (Path("foundation/unicode/docs"), "Unicode", "unicode.md"),
    (Path("foundation/io/docs"), "IO", "io.md"),
    (Path("foundation/filesystem/docs"), "FileSystem", "filesystem.md"),
    (Path("foundation/terminal/docs"), "Terminal", "terminal.md"),
    (Path("engine/window/docs"), "Window", "window_library.md"),
    (Path("tools/logger/docs"), "Logger", "logger.md"),
    (Path("tools/debug/assert/docs"), "Assert", "assert.md"),
    (Path("tools/test_support/docs"), "TestSupport", "test_support.md"),
)

REQUIRED_LIBRARY_DOCS = (
    "quick_start.md",
    "public_api.md",
    "examples.md",
    "testing.md",
    "troubleshooting.md",
)

REQUIRED_LANDING_HEADINGS = (
    "Consumer manual",
    "Maintainer validation",
    "Generated API reference",
    "Key behavior",
    "Dependency boundary",
)

REQUIRED_QUICK_START_HEADINGS = (
    "Include",
    "Installed CMake",
    "Source-tree CMake",
    "Failure handling",
    "Where to go next",
)

DOCUMENTED_SOURCE_ROOTS = (
    Path("foundation/unicode"),
    Path("foundation/io"),
    Path("foundation/filesystem"),
    Path("foundation/terminal"),
    Path("engine/window"),
    Path("tools/logger"),
    Path("tools/debug/assert"),
    Path("tools/test_support"),
    Path("game"),
)

TOP_LEVEL_SIDEBAR = (
    "project_manual",
    "project_reusable_libraries",
    "project_contracts",
    "project_quality_workflows",
    "project_planning",
)

PROJECT_MANUAL_SIDEBAR = (
    "project_getting_started",
    "project_environment_setup",
    "project_structure",
    "project_build",
    "project_command_line_tools",
    "project_tools",
    "project_game_executable",
    "project_validation",
    "project_testing",
    "project_library_compatibility",
    "project_contributing",
    "project_repository_maintenance",
    "project_repository_automation",
    "project_release_automation",
)


def relative(path: Path) -> str:
    return path.relative_to(ROOT).as_posix()


def maintained_manual_files() -> list[Path]:
    files: list[Path] = []
    for root in (ROOT / "docs", ROOT / "foundation", ROOT / "engine", ROOT / "tools"):
        files.extend(path for path in root.rglob("*.md") if "releases" not in path.relative_to(ROOT).parts)
    return sorted(files)


def manual_markup(path: Path) -> str:
    """Return Doxygen markup with fenced examples removed."""
    return FENCED_CODE_PATTERN.sub("", path.read_text(encoding="utf-8"))


def declared_subpages(path: Path) -> tuple[str, ...]:
    return tuple(SUBPAGE_PATTERN.findall(manual_markup(path)))


def check_page_graph(files: list[Path], errors: list[str]) -> None:
    page_owners: dict[str, Path] = {}
    parents: dict[str, set[Path]] = defaultdict(set)

    mainpage_path = ROOT / "docs/doxygen/index.md"
    for path in files:
        text = manual_markup(path)
        page_declarations = list(PAGE_PATTERN.finditer(text))
        expected_page_count = 0 if path == mainpage_path else 1
        if len(page_declarations) != expected_page_count:
            errors.append(f"manual file must declare exactly {expected_page_count} @page entries; found {len(page_declarations)} ({relative(path)})")
        if path == mainpage_path and not re.search(r"^#\s+\S", text):
            errors.append(f"developer manual main page must start with one Markdown title ({relative(path)})")

        for match in page_declarations:
            page_id, title = match.groups()
            previous = page_owners.get(page_id)
            if previous:
                errors.append(f"duplicate Doxygen page ID `{page_id}` in {relative(previous)} and {relative(path)}")
            else:
                page_owners[page_id] = path
            if not title[0].isupper():
                errors.append(f"page title must use sentence case in {relative(path)}: `{title}`")

        for child in SUBPAGE_PATTERN.findall(text):
            parents[child].add(path)

    for child, owners in sorted(parents.items()):
        if child not in page_owners:
            errors.append(f"sidebar references undefined page `{child}`")
        if len(owners) > 1:
            locations = ", ".join(relative(path) for path in sorted(owners))
            errors.append(f"sidebar page `{child}` has multiple parents: {locations}")

    for page_id, path in sorted(page_owners.items()):
        owner_count = len(parents.get(page_id, set()))
        if owner_count != 1:
            errors.append(f"manual page `{page_id}` must have exactly one sidebar parent; found {owner_count} ({relative(path)})")


def check_sidebar_order(errors: list[str]) -> None:
    index_children = declared_subpages(ROOT / "docs/doxygen/index.md")
    if index_children != TOP_LEVEL_SIDEBAR:
        errors.append("developer-manual top-level sidebar order changed; expected " + ", ".join(TOP_LEVEL_SIDEBAR))

    manual_children = declared_subpages(ROOT / "docs/doxygen/project_manual.md")
    if manual_children != PROJECT_MANUAL_SIDEBAR:
        errors.append("project-manual sidebar order changed; expected " + ", ".join(PROJECT_MANUAL_SIDEBAR))


def check_project_registration(errors: list[str]) -> None:
    registration = (ROOT / "cmake/GameWIPDocumentation.cmake").read_text(encoding="utf-8")
    project_pages = sorted((ROOT / "docs/doxygen").glob("*.md"))
    project_pages.extend(sorted((ROOT / "docs").glob("*.md")))

    for path in project_pages:
        if path.parent.name == "doxygen":
            marker = chr(36) + "{GAMEWIP_DOXYGEN_ROOT}/" + path.name
        else:
            marker = chr(36) + "{PROJECT_SOURCE_DIR}/docs/" + path.name
        if marker not in registration:
            errors.append(f"project manual page is not explicitly registered: {relative(path)}")


def check_library_child_titles(errors: list[str]) -> None:
    for root, library_name, landing_name in LIBRARY_DOCS:
        for path in sorted((ROOT / root).glob("*.md")):
            if path.name == landing_name:
                continue
            match = PAGE_PATTERN.search(path.read_text(encoding="utf-8"))
            if not match:
                errors.append(f"library manual file has no @page declaration: {relative(path)}")
                continue
            title = match.group(2)
            if title.startswith(f"{library_name} "):
                errors.append(f"library child title repeats its sidebar parent in {relative(path)}: `{title}`")


def check_required_library_docs(errors: list[str]) -> None:
    for root, _library_name, landing_name in LIBRARY_DOCS:
        required_files = (landing_name, *REQUIRED_LIBRARY_DOCS)
        for filename in required_files:
            path = ROOT / root / filename
            if not path.is_file():
                errors.append(f"required library manual file is missing: {relative(path)}")

        landing_path = ROOT / root / landing_name
        if landing_path.is_file():
            headings = set(re.findall(r"^##\s+(.+?)\s*$", manual_markup(landing_path), re.MULTILINE))
            for heading in REQUIRED_LANDING_HEADINGS:
                if heading not in headings:
                    errors.append(f"library landing page lacks required `{heading}` section: {relative(landing_path)}")

        quick_start_path = ROOT / root / "quick_start.md"
        if quick_start_path.is_file():
            headings = set(re.findall(r"^##\s+(.+?)\s*$", manual_markup(quick_start_path), re.MULTILINE))
            for heading in REQUIRED_QUICK_START_HEADINGS:
                if heading not in headings:
                    errors.append(f"library quick start lacks required `{heading}` section: {relative(quick_start_path)}")
            if not any(heading.startswith("Minimal") for heading in headings):
                errors.append(f"library quick start lacks a minimal-usage section: {relative(quick_start_path)}")


def check_source_file_headers(errors: list[str]) -> None:
    for source_root in DOCUMENTED_SOURCE_ROOTS:
        for path in sorted((ROOT / source_root).rglob("*")):
            if not path.is_file() or not path.name.endswith((".h", ".h.in", ".cpp", ".inl")):
                continue
            header = "\n".join(path.read_text(encoding="utf-8").splitlines()[:12])
            if "@file" not in header or "@brief" not in header:
                errors.append(f"documented source lacks leading @file/@brief ownership: {relative(path)}")


def check_command_catalog_documentation(errors: list[str]) -> None:
    helper_path = ROOT / "scripts/GameWIP.ps1"
    helper_text = helper_path.read_text(encoding="utf-8")
    command_config = json.loads((ROOT / "scripts/config/commands.json").read_text(encoding="utf-8"))
    command_page = (ROOT / "docs/doxygen/command_line_tools.md").read_text(encoding="utf-8")
    for action in (entry["id"] for entry in command_config["actions"]):
        if f"| `{action}` |" not in command_page:
            errors.append(f"project-helper action `{action}` is absent from the command reference")
    parameter_block = helper_text.split(")\n\nSet-StrictMode", maxsplit=1)[0]
    parameter_names = re.findall(
        r"^\s*\[(?:string|int|string\[\]|switch)\]\$(\w+)",
        parameter_block,
        re.MULTILINE,
    )
    for parameter in parameter_names:
        if parameter == "Action":
            continue
        if f"-{parameter}" not in command_page:
            errors.append(f"project-helper option `-{parameter}` is absent from the command reference")

    setup_config_path = ROOT / "scripts/setup/config/setup.json"
    setup_config = json.loads(setup_config_path.read_text(encoding="utf-8"))
    setup_manual = (ROOT / "docs/doxygen/environment_setup.md").read_text(encoding="utf-8")
    for action in (entry["id"] for entry in setup_config["actions"]):
        marker = "`setup.bat` or `setup.bat menu`" if action == "menu" else f"`setup.bat {action}`"
        if marker not in setup_manual:
            errors.append(f"setup action `{action}` is absent from the environment manual")


def main() -> int:
    errors: list[str] = []
    files = maintained_manual_files()
    check_page_graph(files, errors)
    check_sidebar_order(errors)
    check_project_registration(errors)
    check_library_child_titles(errors)
    check_required_library_docs(errors)
    check_source_file_headers(errors)
    check_command_catalog_documentation(errors)

    if errors:
        print("Documentation standards failed:")
        for error in errors:
            print(f"- {error}")
        return 1

    print(
        "Documentation standards are valid: unique pages, one-parent sidebar, "
        "registered project docs, complete library manuals, concise library titles, "
        "and owned documented sources."
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
