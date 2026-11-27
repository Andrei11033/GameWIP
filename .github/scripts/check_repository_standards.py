#!/usr/bin/env python3
"""Check repository-wide automation, policy-file, and community standards."""

from __future__ import annotations

import bisect
import json
import os
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
    ROOT / ".github" / "ISSUE_TEMPLATE" / "decision.yml",
    ROOT / ".github" / "ISSUE_TEMPLATE" / "release.yml",
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
            "Change Scope",
            "Packages (CMake)",
            "Build and Test",
            "Repository Checks",
            "Coverage",
            "AddressSanitizer",
            "UndefinedBehaviorSanitizer",
            "Docs Check",
            "Gate",
        ),
    ),
}

ACTION_REFERENCE = re.compile(r"^\s*uses:\s*([^\s#]+)")
ACTION_VERSION_ANNOTATION = re.compile(r"^\s*uses:\s*([^\s#]+)(?:\s+#\s*(\S(?:.*\S)?))?\s*$")
ACTION_VERSION = re.compile(r"^v[0-9]+(?:\.[0-9]+){0,2}$")
VALIDATION_TOOL_REFERENCE = re.compile(r"\btool\s+ensure\s+([a-z0-9][a-z0-9_-]*)", re.IGNORECASE)


FULL_COMMIT_PIN = re.compile(r"^[^\s@]+@[0-9a-f]{40}$")
JOB_START = re.compile(r"^  ([A-Za-z0-9_-]+):\s*$")
JOB_TIMEOUT = re.compile(r"^    timeout-minutes:\s*[1-9][0-9]*\s*$")
CTEST_COMMAND = re.compile(r"^(?:run:\s*)?ctest(?:\s|$)")
CTEST_PRESET = re.compile(r"(?:^|\s)--preset(?:=|\s+)([A-Za-z0-9_.-]+)")
ISSUE_AREA_OPTIONS = re.compile(r"(?ms)^    id: area\s*$.*?^      options:\s*$\n((?:^        - [^\n]+\n?)+)")
ISSUE_AREA_MAPPING = re.compile(r"\[['\"]([^'\"]+)['\"],\s*['\"](area:[^'\"]+)['\"]\]")
CURRENT_AREA_MAPPING_BLOCK = re.compile(r"const AREA_LABELS_BY_VALUE = new Map\(\[(.*?)\]\);", re.DOTALL)
VALIDATION_TEST_MODULE_PATTERN = re.compile(
    r"^[ \t]*gamewip_add_test_module[ \t\r\n]*\([ \t\r\n]*NAME[ \t\r\n]+([A-Za-z0-9_-]+)", re.IGNORECASE | re.MULTILINE
)
CANONICAL_AREA_MAPPINGS = {
    "CMake / build system": "area:cmake",
    "FileSystem": "area:filesystem",
    "IO": "area:io",
    "Terminal": "area:terminal",
    "Unicode": "area:unicode",
    "Foundation shared systems": "area:foundation",
    "Logger": "area:logger",
    "Assert": "area:assert",
    "TestSupport": "area:test-support",
    "Input": "area:input",
    "Action": "area:action",
    "Desktop": "area:desktop",
    "Window manager": "area:window-manager",
    "Engine shared systems": "area:engine",
    "Windows platform backend": "area:platform-win32",
    "Math": "area:math",
    "Rendering": "area:rendering",
    "Simulation": "area:simulation",
    "Audio": "area:audio",
    "Tools / developer support": "area:tools",
    "Documentation": "area:docs",
    "GitHub / repository setup": "area:github",
    "Roadmap / planning": "area:roadmap",
    "Gameplay": "area:gameplay",
}
GENERAL_ISSUE_FORMS = {
    "bug_report.yml": ("bug: ", "type:bug", "priority:normal"),
    "feature_request.yml": ("feature: ", "type:feature", "priority:normal"),
    "task.yml": ("task: ", "type:task", "priority:normal"),
    "decision.yml": ("decision: ", "type:decision", "priority:normal"),
}
SUPPORTED_PROVIDERS = {
    "msys2",
    "npm",
    "python",
    "powershellGallery",
    "githubRelease",
    "archive",
    "winget",
    "external",
}

APACHE_LICENSE_MARKERS = (
    "Apache License\n                           Version 2.0, January 2004",
    "TERMS AND CONDITIONS FOR USE, REPRODUCTION, AND DISTRIBUTION",
    "END OF TERMS AND CONDITIONS",
)

MAINTAINED_CPP_SUFFIXES = {
    ".c",
    ".cc",
    ".cpp",
    ".cxx",
    ".h",
    ".hh",
    ".hpp",
    ".hxx",
    ".inl",
    ".ipp",
}

SOURCE_DOCUMENTATION_SUFFIXES = MAINTAINED_CPP_SUFFIXES | {
    ".cmake",
    ".js",
    ".mjs",
    ".cjs",
    ".ps1",
    ".psd1",
    ".psm1",
    ".py",
}

FORBIDDEN_UNICODE_CONVERSIONS = re.compile(
    r"\b(?:MultiByteToWideChar|WideCharToMultiByte|mbstowcs|wcstombs|mbrtoc16|c16rtomb|mbrtoc32|c32rtomb)\b|"
    r"std\s*::\s*(?:wstring_convert|codecvt)",
)
SECTION_DIVIDER = re.compile(r"^(?P<indent>\s*)(?P<marker>//|#)\s*(?P<dashes>-{10,})\s*$")
SECTION_TITLE = re.compile(r"^(?P<indent>\s*)(?P<marker>//|#)\s*(?P<title>\S(?:.*\S)?)\s*$")
DOXYGEN_GROUP_OPEN = re.compile(r"^\s*/// @\{\s*$")
CANONICAL_DIVIDER = "-" * 60


def validation_tool_ids(text: str) -> set[str]:
    """Return tool IDs explicitly provisioned by the validation workflow."""
    return {match.group(1).lower() for match in VALIDATION_TOOL_REFERENCE.finditer(text)}


# ------------------------------------------------------------
# Repository metadata
# ------------------------------------------------------------


def check_license_metadata(failures: list[str]) -> None:
    """Require one coherent first-party license and attribution boundary."""
    license_path = ROOT / "LICENSE"
    notice_path = ROOT / "NOTICE"
    readme_path = ROOT / "README.md"
    contributing_path = ROOT / "docs" / "contributing.md"
    decisions_path = ROOT / "docs" / "decisions.md"

    if license_path.is_file():
        license_text = read_text("LICENSE", failures)

        if license_text is not None:
            for marker in APACHE_LICENSE_MARKERS:
                if marker not in license_text:
                    failures.append(f"LICENSE: canonical Apache-2.0 marker is missing: {marker!r}")

    if notice_path.is_file():
        notice_text = read_text("NOTICE", failures)

        if notice_text is not None:
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
        relative = metadata_path.relative_to(ROOT).as_posix()
        text = read_text(relative, failures)

        if text is None:
            continue

        for marker in markers:
            if marker not in text:
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


# ------------------------------------------------------------
# GitHub policy
# ------------------------------------------------------------


def check_issue_area_mapping(failures: list[str]) -> None:
    """Require issue forms, area automation, and generated labels to share one taxonomy."""
    workflow_path = WORKFLOW_ROOT / "issue-area-labels.yml"

    if not workflow_path.is_file():
        return
    policy_path = ROOT / ".github" / "scripts" / "issue-area-labels.js"

    if not policy_path.is_file():
        failures.append(".github/scripts/issue-area-labels.js: extracted area policy is missing")
        return
    policy_text = read_text(".github/scripts/issue-area-labels.js", failures)

    if policy_text is None:
        return

    mapping_block = CURRENT_AREA_MAPPING_BLOCK.search(policy_text)
    mappings = dict(ISSUE_AREA_MAPPING.findall(mapping_block.group(1))) if mapping_block else {}

    # Match the extracted policy to the canonical area taxonomy.
    if mappings != CANONICAL_AREA_MAPPINGS:
        failures.append(".github/scripts/issue-area-labels.js: current area mapping does not match the canonical taxonomy")

    expected_options = list(CANONICAL_AREA_MAPPINGS)

    # Keep general issue forms aligned with the full ordered area list.
    for name, (title, type_label, priority_label) in GENERAL_ISSUE_FORMS.items():
        path = ROOT / ".github" / "ISSUE_TEMPLATE" / name
        relative = path.relative_to(ROOT).as_posix()
        text = read_text(relative, failures)

        if text is None:
            continue

        match = ISSUE_AREA_OPTIONS.search(text)

        if match is None:
            failures.append(f"{relative}: area dropdown was not found")
            continue
        options = [line.removeprefix("        - ").strip() for line in match.group(1).splitlines()]

        if options != expected_options:
            failures.append(f"{relative}: area dropdown does not match the canonical ordered taxonomy")

        for required in (
            f"title: '{title}'",
            f"  - '{type_label}'",
            f"  - '{priority_label}'",
        ):
            if required not in text:
                failures.append(f"{relative}: required issue-form metadata is missing: {required}")

    # Keep the release form on its fixed metadata instead of a general area choice.
    release_text = read_text(".github/ISSUE_TEMPLATE/release.yml", failures)

    if release_text is None:
        return

    if ISSUE_AREA_OPTIONS.search(release_text) is not None:
        failures.append(".github/ISSUE_TEMPLATE/release.yml: release form must not expose the general Area dropdown")

    for required in (
        "title: 'release: '",
        "  - 'type:release'",
        "  - 'area:github'",
        "  - 'priority:high'",
    ):
        if required not in release_text:
            failures.append(f".github/ISSUE_TEMPLATE/release.yml: required release metadata is missing: {required}")

    # Apply the default repository labels to every Dependabot ecosystem.
    dependabot = read_text(".github/dependabot.yml", failures)

    if dependabot is None:
        return
    ecosystem_count = dependabot.count("  - package-ecosystem:")

    if ecosystem_count == 0:
        failures.append(".github/dependabot.yml: no ecosystems are configured")

    for label in ("area:github", "type:task", "priority:normal"):
        if dependabot.count(f"      - '{label}'") != ecosystem_count:
            failures.append(f".github/dependabot.yml: every ecosystem must create '{label}'")


def check_github_metadata_policy(failures: list[str]) -> None:
    """Prevent current metadata cardinality and ownership rules from drifting."""
    contributing = read_text("docs/contributing.md", failures)

    if contributing is not None and (
        re.search(
            r"exactly one `type:\*`, one `area:\*`, and one\s+`priority:\*`",
            contributing,
        )
        is None
    ):
        failures.append("docs/contributing.md: exact primary metadata cardinality guidance is missing")

    if contributing is not None:
        for required in (
            "`compat:breaking`",
            "<area>/<issue-number>-<short-summary>",
            "A GitHub milestone means",
        ):
            if required not in contributing:
                failures.append(f"docs/contributing.md: required GitHub metadata guidance is missing: {required}")

    pr_standards = read_text(".github/scripts/pr-standards.js", failures)

    if pr_standards is not None:
        for required in (
            "type:bug",
            "type:feature",
            "type:task",
            "type:decision",
            "type:release",
            "compat:breaking",
        ):
            if required not in pr_standards:
                failures.append(f".github/scripts/pr-standards.js: canonical metadata value is missing: {required}")


# ------------------------------------------------------------
# Maintained-file registries
# ------------------------------------------------------------


def read_text(relative: str, failures: list[str], *, errors: str = "strict") -> str | None:
    """Read UTF-8 text and report failures without stopping other checks."""
    try:
        return (ROOT / relative).read_text(encoding="utf-8", errors=errors)
    except (OSError, UnicodeError) as error:
        failures.append(f"{relative}: cannot read file: {error}")
        return None


def read_json(relative: str, failures: list[str]) -> dict:
    """Read a required JSON authority while retaining all failures."""
    text = read_text(relative, failures)

    if text is None:
        return {}

    try:
        data = json.loads(text)
    except json.JSONDecodeError as error:
        failures.append(f"{relative}: cannot read JSON authority: {error}")
        return {}

    if not isinstance(data, dict):
        failures.append(f"{relative}: JSON authority must contain an object")
        return {}

    return data


def is_directory_link(path: Path) -> bool:
    """Return whether a path is a symlink or supported junction."""
    if path.is_symlink():
        return True

    is_junction = getattr(path, "is_junction", None)
    return bool(is_junction is not None and is_junction())


def maintained_files() -> list[Path]:
    """Return first-party files without generated or disposable trees."""
    excluded = {".git", "build", "external", "generated", "vendor"}
    files: list[Path] = []

    for directory, children, names in os.walk(ROOT):
        current = Path(directory)

        # Prune generated trees and directory links before descending into them.
        children[:] = sorted(name for name in children if name not in excluded and not is_directory_link(current / name))

        for name in names:
            path = current / name

            if path.is_file():
                files.append(path)

    return sorted(files, key=lambda path: path.relative_to(ROOT).as_posix())


def source_documentation_files(files: list[Path]) -> list[Path]:
    """Return maintained source files that support line comments."""
    return [path for path in files if path.suffix.lower() in SOURCE_DOCUMENTATION_SUFFIXES or path.name == "CMakeLists.txt"]


def validation_test_module_names(failures: list[str]) -> list[str]:
    """Return module IDs registered by validation-test CMake files."""
    module_root = ROOT / "game/validation/tests"
    module_names = []

    for path in module_root.glob("*/CMakeLists.txt"):
        relative = path.relative_to(ROOT).as_posix()
        text = read_text(relative, failures)

        if text is not None:
            module_names.extend(VALIDATION_TEST_MODULE_PATTERN.findall(text))

    return sorted(module_names)


def is_cpp_numeric_digit_separator(text: str, index: int) -> bool:
    """Return whether an apostrophe is a C++ numeric-literal digit separator."""

    if index == 0 or index + 1 >= len(text) or text[index - 1] not in "0123456789abcdefABCDEF" or text[index + 1] not in "0123456789abcdefABCDEF":
        return False

    token_start = index - 1

    while token_start > 0 and (text[token_start - 1].isalnum() or text[token_start - 1] in "._"):
        token_start -= 1
    token = text[token_start:index]
    return token[0].isdigit() or (token[0] == "." and len(token) > 1 and token[1].isdigit())


def strip_cpp_comments_and_literals(text: str) -> str:
    """Blank C++ comments and literals while preserving line numbers for policy diagnostics."""
    output: list[str] = []
    index = 0
    state = "code"
    literal_delimiter = ""
    raw_terminator = ""

    while index < len(text):
        current = text[index]
        following = text[index + 1] if index + 1 < len(text) else ""

        if state == "code":
            if current == "/" and following == "/":
                output.extend((" ", " "))
                index += 2
                state = "line-comment"
                continue

            if current == "/" and following == "*":
                output.extend((" ", " "))
                index += 2
                state = "block-comment"
                continue

            if current == "R" and following == '"':
                opening_parenthesis = text.find("(", index + 2, index + 19)

                if opening_parenthesis != -1:
                    delimiter = text[index + 2 : opening_parenthesis]

                    if all(character not in " \\()\t\r\n" for character in delimiter):
                        raw_terminator = ")" + delimiter + '"'
                        output.extend(" " for _ in range(opening_parenthesis + 1 - index))
                        index = opening_parenthesis + 1
                        state = "raw-literal"
                        continue

            if current == "'" and is_cpp_numeric_digit_separator(text, index):
                output.append(current)
                index += 1
                continue

            if current in {'"', "'"}:
                output.append(" ")
                literal_delimiter = current
                index += 1
                state = "literal"
                continue
            output.append(current)
            index += 1
            continue

        if state == "line-comment":
            if current in {"\r", "\n"}:
                output.append(current)
                state = "code"
            else:
                output.append(" ")
            index += 1
            continue

        if state == "raw-literal":
            if text.startswith(raw_terminator, index):
                output.extend(" " for _ in raw_terminator)
                index += len(raw_terminator)
                raw_terminator = ""
                state = "code"
            else:
                output.append(current if current in {"\r", "\n"} else " ")
                index += 1
            continue

        if state == "block-comment":
            if current == "*" and following == "/":
                output.extend((" ", " "))
                index += 2
                state = "code"
            else:
                output.append(current if current in {"\r", "\n"} else " ")
                index += 1
            continue

        if current == "\\" and index + 1 < len(text):
            output.append(" ")
            output.append(text[index + 1] if text[index + 1] in {"\r", "\n"} else " ")
            index += 2

        elif current in {"\r", "\n"}:
            output.append(current)
            index += 1
        else:
            output.append(" ")
            index += 1

        if current == literal_delimiter:
            state = "code"
            literal_delimiter = ""

    return "".join(output)


def check_unicode_conversion_authority(failures: list[str], files: list[Path]) -> None:
    """Reject independent maintained C++ encoding conversions."""

    for path in files:
        relative = path.relative_to(ROOT).as_posix()

        if path.suffix.lower() not in MAINTAINED_CPP_SUFFIXES:
            continue
        source = read_text(relative, failures)

        if source is None:
            continue

        source = strip_cpp_comments_and_literals(source)
        line_starts = [0, *(match.end() for match in re.finditer("\\n", source))]

        for match in FORBIDDEN_UNICODE_CONVERSIONS.finditer(source):
            line_number = bisect.bisect_right(line_starts, match.start())
            failures.append(f"{relative}:{line_number}: Unicode encoding conversion must use foundation/unicode.")


def normalize_source_documentation(text: str) -> tuple[str, list[tuple[int, str]]]:
    """Return canonical source-documentation layout and actionable failures."""
    newline = "\r\n" if "\r\n" in text else "\n"
    trailing_newline = text.endswith(("\n", "\r"))
    lines = text.splitlines()
    normalized: list[str] = []
    failures: list[tuple[int, str]] = []
    index = 0

    while index < len(lines):
        line = lines[index]
        divider = SECTION_DIVIDER.fullmatch(line)

        if divider is not None:
            title = SECTION_TITLE.fullmatch(lines[index + 1]) if index + 1 < len(lines) else None
            closing = SECTION_DIVIDER.fullmatch(lines[index + 2]) if index + 2 < len(lines) else None

            if (
                title is None
                or closing is None
                or title.group("indent") != divider.group("indent")
                or title.group("marker") != divider.group("marker")
                or closing.group("indent") != divider.group("indent")
                or closing.group("marker") != divider.group("marker")
            ):
                failures.append(
                    (
                        index + 1,
                        "section separator must be a matching divider, title, and divider block",
                    )
                )
                normalized.append(line)
                index += 1
                continue

            canonical = f"{divider.group('indent')}{divider.group('marker')} {CANONICAL_DIVIDER}"
            canonical_title = f"{divider.group('indent')}{divider.group('marker')} {title.group('title')}"

            if lines[index : index + 3] != [canonical, canonical_title, canonical]:
                failures.append(
                    (
                        index + 1,
                        "section separator must use one space after the comment marker and exactly 60 hyphens",
                    )
                )
            normalized.extend((canonical, canonical_title, canonical))
            index += 3
            continue

        normalized.append(line)

        if DOXYGEN_GROUP_OPEN.fullmatch(line) and (index + 1 >= len(lines) or lines[index + 1].strip()):
            failures.append((index + 1, "Doxygen group opening must be followed by a blank line"))
            normalized.append("")
        index += 1

    result = newline.join(normalized)

    if trailing_newline:
        result += newline
    return result, failures


def check_source_documentation_layout(failures: list[str], files: list[Path]) -> None:
    """Require canonical section separators and unambiguous Doxygen groups."""

    for path in source_documentation_files(files):
        relative = path.relative_to(ROOT).as_posix()
        text = read_text(relative, failures, errors="replace")

        if text is None:
            continue

        _, layout_failures = normalize_source_documentation(text)
        failures.extend(f"{relative}:{line}: {message}" for line, message in layout_failures)


def fix_source_documentation_layout(requested: list[str]) -> int:
    """Normalize mechanically safe source-documentation layout details."""
    changed = 0
    unsafe: list[str] = []
    paths = source_documentation_files(maintained_files())

    # Restrict writes to explicitly requested paths when provided.
    if requested:
        requested_paths = {(ROOT / item).resolve() if not Path(item).is_absolute() else Path(item).resolve() for item in requested}
        paths = [path for path in paths if path.resolve() in requested_paths]

    for path in paths:
        relative = path.relative_to(ROOT).as_posix()

        try:
            text = path.read_text(encoding="utf-8", errors="strict")
        except (OSError, UnicodeError) as error:
            unsafe.append(f"{relative}: cannot read source file: {error}")
            continue

        normalized, layout_failures = normalize_source_documentation(text)
        structural = [failure for failure in layout_failures if "matching divider" in failure[1]]

        # Leave malformed separator blocks untouched and report them together.
        if structural:
            unsafe.extend(f"{relative}:{line}: {message}" for line, message in structural)
            continue

        # Write only when safe normalization changes the file.
        if normalized != text:
            try:
                path.write_text(normalized, encoding="utf-8", newline="")
            except OSError as error:
                unsafe.append(f"{relative}: cannot write normalized source file: {error}")
                continue
            changed += 1

    if unsafe:
        print(
            "Source-documentation formatting stopped on unsafe structure:",
            file=sys.stderr,
        )

        for failure in unsafe:
            print(f"- {failure}", file=sys.stderr)
        return 1
    print(f"Normalized source-documentation layout in {changed} file(s).")
    return 0


def check_registry_relationships(failures: list[str]) -> None:
    """Validate cross-file authorities and repository-facing relationships."""

    # Load the registries and documentation used by the cross-file checks.
    commands = read_json("scripts/config/commands.json", failures)
    project = read_json("scripts/config/project.json", failures)
    tools = read_json("scripts/config/project-tools.json", failures)
    setup = read_json("scripts/setup/config/setup.json", failures)
    helper = read_text("scripts/GameWIP.ps1", failures) or ""
    build_docs = "\n".join(read_text(relative, failures) or "" for relative in ("docs/doxygen/build.md", "docs/doxygen/command_line_tools.md"))

    # Keep documented build options and visible presets current.
    option_text = read_text("cmake/GameWIPOptions.cmake", failures) or ""
    options = set(re.findall(r"(?m)^option\((GAMEWIP_[A-Z0-9_]+)\s", option_text))
    options.update(re.findall(r"(?m)^set\((GAMEWIP_[A-Z0-9_]+)\s+[^\n]*CACHE\s", option_text))

    for option in sorted(options):
        if option not in build_docs:
            failures.append(f"cmake/GameWIPOptions.cmake: live build docs omit {option}")

    presets = read_json("CMakePresets.json", failures)
    visible = {
        preset["name"] for kind in ("configurePresets", "buildPresets", "testPresets") for preset in presets.get(kind, []) if not preset.get("hidden")
    }

    for preset in sorted(visible):
        if f"`{preset}`" not in build_docs:
            failures.append(f"CMakePresets.json: live build docs omit visible preset '{preset}'")

    workflow_text = "\n".join(read_text(path.relative_to(ROOT).as_posix(), failures) or "" for path in WORKFLOW_ROOT.glob("*.yml"))
    workflow_defines = set(re.findall(r"-D(GAMEWIP_[A-Z0-9_]+)=", workflow_text))

    for variable in sorted(workflow_defines - options):
        failures.append(f".github/workflows: CMake -D variable '{variable}' is not a current option")

    # Keep command, validation-module, and workflow catalogs aligned.
    for required_action in (
        "quality",
        "tool",
        "list",
        "ready",
        "unicode",
        "workflow",
    ):
        if f"'{required_action}'" not in helper:
            failures.append(f"scripts/GameWIP.ps1: helper action '{required_action}' is not dispatched")

    discovered_module_names = validation_test_module_names(failures)
    discovered_modules = set(discovered_module_names)
    configured_modules = {module["id"] for module in commands.get("modules", [])}

    if len(discovered_module_names) != len(discovered_modules):
        failures.append("game/validation/tests: duplicate registered validation-test module names")

    if discovered_modules != configured_modules:
        failures.append(
            "scripts/config/commands.json: modules differ from validation-test CMake registrations "
            f"(configured={sorted(configured_modules)}, discovered={sorted(discovered_modules)})"
        )

    workflow_files = {path.name for path in WORKFLOW_ROOT.glob("*.yml")}

    for entry in commands.get("manualWorkflows", []):
        if entry.get("file") not in workflow_files:
            failures.append(f"scripts/config/commands.json: workflow '{entry.get('id')}' names missing file '{entry.get('file')}'")

    for forbidden in ("wingetPackages", "msys2Packages", "cmakeVersionPattern"):
        if forbidden in setup:
            failures.append(f"scripts/setup/config/setup.json: duplicate tool authority '{forbidden}' is forbidden")

    # Validate tool providers and the source references they maintain.
    tool_entries = tools.get("tools", [])
    tool_by_id = {entry.get("id"): entry for entry in tool_entries}

    if len(tool_by_id) != len(tool_entries):
        failures.append("scripts/config/project-tools.json: duplicate tool IDs")

    for tool_id in setup.get("bootstrapToolIds", []):
        if tool_id not in tool_by_id:
            failures.append(f"scripts/setup/config/setup.json: bootstrap tool '{tool_id}' is not registered")

    root_cmake = read_text("CMakeLists.txt", failures) or ""
    cmake_match = re.search(r"(?m)^cmake_minimum_required\(VERSION\s+([0-9.]+)\)", root_cmake)
    cmake_tool = tool_by_id.get("cmake", {})

    if cmake_match is None:
        failures.append("CMakeLists.txt: cmake_minimum_required version was not found")

    elif cmake_tool.get("requiredVersion") != cmake_match.group(1):
        failures.append(
            "scripts/config/project-tools.json: CMake minimum must equal root cmake_minimum_required "
            f"({cmake_tool.get('requiredVersion')!r} != {cmake_match.group(1)!r})"
        )

    # Providers may be shared or host-specific.
    provider_files = {path.stem.lower() for path in (ROOT / "scripts/lib/Providers").glob("*.ps1")}
    provider_files.update(
        path.stem.lower() for platform in (ROOT / "scripts/platform").iterdir() if platform.is_dir() for path in platform.glob("*.ps1")
    )
    validation_text = read_text(".github/workflows/validation.yml", failures) or ""
    validation_tools = validation_tool_ids(validation_text)
    tool_docs = read_text("docs/doxygen/project_tools.md", failures) or ""
    live_references = set()
    repository_root = ROOT.resolve()

    for tool_id in validation_tools:
        if tool_id not in tool_by_id:
            failures.append(f".github/workflows/validation.yml: tool '{tool_id}' is not registered in project-tool metadata")

    for entry in tool_entries:
        tool_id = entry.get("id")
        provider_info = entry.get("provider", {})
        provider = provider_info.get("kind")

        if provider not in SUPPORTED_PROVIDERS:
            failures.append(f"scripts/config/project-tools.json: tool '{tool_id}' uses unsupported provider '{provider}'")

        elif provider.lower() not in provider_files:
            failures.append(f"scripts/lib/Providers or scripts/platform/<platform>: provider implementation '{provider}' is missing")

        if provider == "msys2":
            if provider_info.get("environment") not in {"common", "ucrt64", "clang64"}:
                failures.append(f"scripts/config/project-tools.json: MSYS2 tool '{tool_id}' lacks a valid environment")

            if not provider_info.get("package"):
                failures.append(f"scripts/config/project-tools.json: MSYS2 tool '{tool_id}' lacks a package")

            for dependency in provider_info.get("dependencies", []):
                if dependency.get("environment") not in {"common", "ucrt64", "clang64"}:
                    failures.append(
                        f"scripts/config/project-tools.json: MSYS2 dependency '{dependency.get('package')}' for '{tool_id}' lacks a valid environment"
                    )

        if provider == "npm":
            for dependency in provider_info.get("dependencies", []):
                if not dependency.get("version"):
                    failures.append(
                        f"scripts/config/project-tools.json: npm dependency '{dependency.get('package')}' for '{tool_id}' is not versioned"
                    )

        if provider == "githubRelease" and not provider_info.get("releaseTag"):
            failures.append(f"scripts/config/project-tools.json: GitHub release tool '{tool_id}' does not retain its actual upstream release tag")

        if provider == "archive":
            if entry.get("versionPolicy") != "exact":
                failures.append(f"scripts/config/project-tools.json: archive tool '{tool_id}' must use an exact version pin")
            if not provider_info.get("baseUrl"):
                failures.append(f"scripts/config/project-tools.json: archive tool '{tool_id}' lacks its download base URL")
            for asset_key, asset in provider_info.get("assets", {}).items():
                if not asset.get("archive") or len(asset.get("sha256", "")) != 64 or not asset.get("validationFiles"):
                    failures.append(f"scripts/config/project-tools.json: archive tool '{tool_id}' has incomplete asset metadata for '{asset_key}'")

        version = entry.get("requiredVersion", "")

        for reference in entry.get("references", []):
            if not isinstance(reference, dict):
                failures.append(f"scripts/config/project-tools.json: tool '{tool_id}' has a non-object reference")
                continue
            relative = reference.get("path", "")
            path = (ROOT / relative).resolve()

            if repository_root not in path.parents:
                failures.append(f"scripts/config/project-tools.json: tool '{tool_id}' reference '{relative}' resolves outside the repository")
                continue

            if not path.is_file():
                failures.append(f"scripts/config/project-tools.json: tool '{tool_id}' references missing path '{relative}'")
                continue
            kind = reference.get("kind")

            if kind == "text":
                pattern = reference.get("pattern", "")

                if pattern.count("{version}") != 1:
                    failures.append(
                        f"scripts/config/project-tools.json: text reference '{relative}' for '{tool_id}' must contain exactly one literal {{version}} token"
                    )
                    continue
                expected = int(reference.get("expectedCount", 1))
                concrete = pattern.replace("{version}", version)
                text = read_text(relative, failures)

                if text is None:
                    continue

                count = text.count(concrete)

                if count != expected:
                    failures.append(f"{relative}: live version reference for '{tool_id}' expected {expected} exact match(es), found {count}")

            elif kind == "releaseAsset":
                pattern = reference.get("pattern", "")
                asset_key = reference.get("assetKey")
                asset_field = reference.get("assetField")
                provider = tool_by_id.get(tool_id, {}).get("provider", {})
                asset = provider.get("assets", {}).get(asset_key, {})

                if provider.get("kind") != "githubRelease" or asset_field not in {"archive", "sha256"} or asset_field not in asset:
                    failures.append(
                        f"scripts/config/project-tools.json: release-asset reference for '{tool_id}' does not match a declared GitHub asset"
                    )
                    continue

                if pattern.count("{value}") != 1:
                    failures.append(
                        f"scripts/config/project-tools.json: release-asset reference '{relative}' for '{tool_id}' must contain exactly one literal {{value}} token"
                    )
                    continue
                expected = int(reference.get("expectedCount", 1))
                concrete = pattern.replace("{value}", str(asset[asset_field]))
                text = read_text(relative, failures)

                if text is None:
                    continue

                count = text.count(concrete)

                if count != expected:
                    failures.append(f"{relative}: live release-asset reference for '{tool_id}' expected {expected} exact match(es), found {count}")

            elif kind == "cmakeMinimum":
                expected = int(reference.get("expectedCount", 1))
                concrete = f"cmake_minimum_required(VERSION {version})"
                text = read_text(relative, failures)

                if text is None:
                    continue

                count = text.count(concrete)

                if count != expected:
                    failures.append(f"{relative}: CMake minimum reference for '{tool_id}' expected {expected} exact match(es), found {count}")

                if "pattern" in reference:
                    failures.append(
                        f"scripts/config/project-tools.json: CMake minimum reference '{relative}' for '{tool_id}' must not declare pattern"
                    )

            elif kind == "path":
                if "pattern" in reference or "expectedCount" in reference:
                    failures.append(
                        f"scripts/config/project-tools.json: informational path reference '{relative}' for '{tool_id}' must not declare pattern or expectedCount"
                    )

            elif kind not in {"path", "text", "releaseAsset", "cmakeMinimum"}:
                failures.append(f"scripts/config/project-tools.json: tool '{tool_id}' uses unknown reference kind '{kind}'")

            if kind in {"text", "releaseAsset", "cmakeMinimum"}:
                live_key = (
                    kind,
                    str(path),
                    reference.get("pattern", ""),
                    int(reference.get("expectedCount", 1)),
                )

                if live_key in live_references:
                    failures.append(f"scripts/config/project-tools.json: duplicate live reference '{relative}' for '{tool_id}'")
                live_references.add(live_key)

        if entry.get("versionPolicy") == "exact" and version not in tool_docs:
            failures.append(f"docs/doxygen/project_tools.md: exact tool '{tool_id}' version '{version}' is stale")

    # Keep repository storage and registry schemas under their declared authorities.
    expected_storage = {
        "root": "build/gamewip",
        "cache": "cache",
        "state": "state",
        "temp": "temp",
        "runs": "runs",
    }

    if project.get("storage") != expected_storage:
        failures.append("scripts/config/project.json: repository-local storage must use build/gamewip/{cache,state,temp,runs}")

    expected_managed_environment = {
        "msys2Root": "tools/msys2",
        "gameWipToolsRoot": "tools/managed",
    }

    if project.get("managedEnvironment") != expected_managed_environment:
        failures.append("scripts/config/project.json: managed environments must remain below build/gamewip/tools")

    registry_schemas = {
        "scripts/config/project.json": "scripts/schemas/project.schema.json",
        "scripts/config/commands.json": "scripts/schemas/commands.schema.json",
        "scripts/config/project-tools.json": "scripts/schemas/project-tools.schema.json",
        "scripts/setup/config/setup.json": "scripts/schemas/setup.schema.json",
        "scripts/setup/config/editors.json": "scripts/schemas/editors.schema.json",
        "config/quality/hygiene.json": "scripts/schemas/hygiene.schema.json",
    }

    for registry, schema in registry_schemas.items():
        if not (ROOT / registry).is_file() or not (ROOT / schema).is_file():
            failures.append(f"{registry}: required schema pairing '{schema}' is missing")


# ------------------------------------------------------------
# Workflow and editor integration
# ------------------------------------------------------------


def check_editor_integration(failures: list[str]) -> None:
    """Validate current editor task names and project-helper invocations."""
    tasks = read_json(".vscode/tasks.json", failures)
    labels = {task.get("label") for task in tasks.get("tasks", [])}

    for label in ("GameWIP: Quality Check", "GameWIP: Tools Status"):
        if label not in labels:
            failures.append(f".vscode/tasks.json: required helper task '{label}' is missing")

    helper_actions = {entry.get("id") for entry in read_json("scripts/config/commands.json", failures).get("actions", [])}

    for task in tasks.get("tasks", []):
        invocations = [("default", task.get("command", ""), task.get("args", []))]
        windows = task.get("windows", {})

        if isinstance(windows, dict):
            invocations.append(("Windows", windows.get("command", ""), windows.get("args", [])))

        for platform, command, args in invocations:
            command = str(command).lower()

            if not isinstance(args, list):
                continue

            action_index = 0 if "gamewip.bat" in command else None

            if action_index is None and args and str(args[0]).lower().endswith("gamewip.sh"):
                action_index = 1

            if action_index is None:
                continue

            if len(args) <= action_index:
                failures.append(f".vscode/tasks.json: task '{task.get('label')}' has no {platform} helper action")
                continue

            action = args[action_index]

            if action not in helper_actions:
                failures.append(f".vscode/tasks.json: task '{task.get('label')}' invokes unknown {platform} helper action '{action}'")


def check_extracted_workflow_logic(failures: list[str]) -> None:
    """Keep substantial GitHub-script policy in tested source files."""
    required = {
        "pr-standards.yml": ("pr-standards.js", "pr-standards.test.js"),
        "issue-area-labels.yml": ("issue-area-labels.js", "issue-area-labels.test.js"),
    }

    for workflow, scripts in required.items():
        relative = f".github/workflows/{workflow}"
        text = read_text(relative, failures)

        if text is None:
            continue

        for script in scripts:
            if not (ROOT / ".github/scripts" / script).is_file():
                failures.append(f".github/workflows/{workflow}: required extracted policy '{script}' is missing")

        if "new Map([" in text or "function " in text or "titlePattern" in text:
            failures.append(f".github/workflows/{workflow}: substantial inline program logic must remain extracted")


def fail_closed_ctest_presets(failures: list[str]) -> set[str]:
    """Return presets whose resolved no-tests action is error."""
    text = read_text("CMakePresets.json", failures)

    if text is None:
        return set()

    try:
        data = json.loads(text)
    except json.JSONDecodeError as error:
        failures.append(f"CMakePresets.json: cannot resolve CTest policy: {error}")
        return set()

    if not isinstance(data, dict):
        failures.append("CMakePresets.json: preset authority must contain an object")
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
    failures: list[str] = []
    text = read_text(relative, failures)

    if text is None:
        return failures

    lines = text.splitlines()

    # Enforce workflow-wide permissions and protected check names.
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

    # Validate action pins and fail-closed CTest behavior.
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

    # Keep privileged workflow execution on trusted code and credentials.
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

    # Require bounded execution time for every workflow job.
    jobs = workflow_jobs(lines)

    if not jobs:
        failures.append(f"{relative}: define at least one job")

    for name, block in jobs:
        if not any(JOB_TIMEOUT.match(line) for line in block):
            failures.append(f"{relative}: job '{name}' must define timeout-minutes")

    return failures


def check_action_reference_consistency(workflows: list[Path], failures: list[str]) -> None:
    """Require one pinned reference and readable version annotation per external action."""

    references: dict[str, tuple[str, str, str, int]] = {}

    for path in workflows:
        relative = path.relative_to(ROOT).as_posix()
        text = read_text(relative, failures)

        if text is None:
            continue

        for line_number, line in enumerate(text.splitlines(), start=1):
            match = ACTION_VERSION_ANNOTATION.match(line)

            if match is None:
                continue

            reference, annotation = match.groups()

            if reference.startswith("./"):
                continue

            if annotation is None or ACTION_VERSION.fullmatch(annotation) is None:
                failures.append(f"{relative}:{line_number}: annotate pinned action '{reference}' with a release version such as '# v4.2.0'")
                continue

            action = reference.rpartition("@")[0]
            previous = references.get(action)

            if previous is not None and (previous[0] != reference or previous[1] != annotation):
                failures.append(
                    f"{relative}:{line_number}: action '{action}' uses '{reference} # {annotation}' but "
                    f"{previous[2]}:{previous[3]} uses '{previous[0]} # {previous[1]}'"
                )
                continue
            references[action] = (reference, annotation, relative, line_number)


# ------------------------------------------------------------
# Quality contract
# ------------------------------------------------------------


def check_quality_contract(failures: list[str]) -> None:
    """Protect quality semantics that would otherwise degrade silently."""

    quality_path = ROOT / "scripts" / "lib" / "Quality" / "Checks.ps1"

    if quality_path.is_file():
        quality = read_text("scripts/lib/Quality/Checks.ps1", failures) or ""

        if "config/quality/eslint.config.js" not in quality:
            failures.append("scripts/lib/Quality/Checks.ps1: maintained ESLint configuration must itself be inside the ESLint scope")

    npm_provider_path = ROOT / "scripts" / "lib" / "Providers" / "Npm.ps1"

    if npm_provider_path.is_file():
        npm_provider = read_text("scripts/lib/Providers/Npm.ps1", failures) or ""

        if r"npm\lib\node_modules" not in npm_provider:
            failures.append("scripts/lib/Providers/Npm.ps1: the managed MSYS2 npm prefix must resolve modules through lib/node_modules")

    yamllint_path = ROOT / "config" / "quality" / "yamllint.yml"

    if yamllint_path.is_file():
        yamllint = read_text("config/quality/yamllint.yml", failures) or ""
        line_length = re.search(r"(?ms)^  line-length:\s*$\n(.*?)(?=^  [A-Za-z_-]+:|\Z)", yamllint)

        if line_length is None or "max: 150" not in line_length.group(1) or "level: error" not in line_length.group(1):
            failures.append("config/quality/yamllint.yml: the shared 150-column YAML limit must remain an error")


# ------------------------------------------------------------
# Entry point
# ------------------------------------------------------------


def main() -> int:
    if len(sys.argv) >= 2 and sys.argv[1] == "--fix-source-documentation":
        return fix_source_documentation_layout(sys.argv[2:])

    if len(sys.argv) != 1:
        print(
            "usage: check_repository_standards.py [--fix-source-documentation [paths...]]",
            file=sys.stderr,
        )
        return 2

    failures: list[str] = []

    # Check required public repository metadata.
    for path in REQUIRED_REPOSITORY_FILES:
        if not path.is_file():
            failures.append(f"{path.relative_to(ROOT).as_posix()}: required repository file is missing")

        elif path.stat().st_size == 0:
            failures.append(f"{path.relative_to(ROOT).as_posix()}: required repository file is empty")

    # Validate registries, source conventions, and shared quality policy.
    check_license_metadata(failures)
    check_issue_area_mapping(failures)
    check_github_metadata_policy(failures)
    check_registry_relationships(failures)
    check_editor_integration(failures)
    maintained = maintained_files()
    check_source_documentation_layout(failures, maintained)
    check_unicode_conversion_authority(failures, maintained)
    check_quality_contract(failures)
    check_extracted_workflow_logic(failures)

    # Validate workflow contracts and protected action references.
    safe_ctest_presets = fail_closed_ctest_presets(failures)
    workflows = sorted((*WORKFLOW_ROOT.glob("*.yml"), *WORKFLOW_ROOT.glob("*.yaml")))

    if not workflows:
        failures.append(".github/workflows: no workflow files found")
    check_action_reference_consistency(workflows, failures)

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
