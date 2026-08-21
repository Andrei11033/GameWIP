#!/usr/bin/env python3
"""Validate local relative links in maintained Markdown documentation."""

from __future__ import annotations

import argparse
import os
import re
import sys
from pathlib import Path
from urllib.parse import unquote, urlsplit

SCANNED_MARKDOWN_DIRS = (
    Path("docs"),
    Path(".github"),
    Path("foundation"),
    Path("engine"),
    Path("tools"),
    Path("game"),
    Path("scripts"),
)
EXCLUDED_ROOTS = (
    Path("external"),
    Path("build"),
)
INLINE_LINK_PATTERN = re.compile(r"(?<!!)\[[^\]\n]*(?:\][^\[]*)?\]\(([^)\n]+)\)")
IMAGE_LINK_PATTERN = re.compile(r"!\[[^\]\n]*(?:\][^\[]*)?\]\(([^)\n]+)\)")
REFERENCE_DEFINITION_PATTERN = re.compile(r"^\s{0,3}\[[^\]]+\]:\s*(\S+)", re.MULTILINE)


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Validate local relative Markdown links in maintained documentation."
    )
    parser.add_argument(
        "--root",
        type=Path,
        default=Path.cwd(),
        help="Repository root. Defaults to the current working directory.",
    )
    return parser.parse_args()


def normalized_relative_path(path: Path, root: Path) -> Path | None:
    try:
        return path.resolve().relative_to(root.resolve())
    except ValueError:
        return None


def is_under(path: Path, root: Path) -> bool:
    path_parts = path.parts
    root_parts = root.parts
    return path_parts[: len(root_parts)] == root_parts


def is_excluded(path: Path) -> bool:
    normalized = Path(os.path.normpath(path.as_posix()))
    return any(is_under(normalized, excluded_root) for excluded_root in EXCLUDED_ROOTS)


def iter_markdown_files(root: Path) -> list[Path]:
    markdown_files: set[Path] = set()

    for file_path in root.glob("*.md"):
        relative_file_path = normalized_relative_path(file_path, root)
        if relative_file_path is not None and not is_excluded(relative_file_path):
            markdown_files.add(file_path)

    for directory in SCANNED_MARKDOWN_DIRS:
        absolute_directory = root / directory
        if not absolute_directory.is_dir():
            continue
        for file_path in absolute_directory.rglob("*.md"):
            relative_file_path = normalized_relative_path(file_path, root)
            if relative_file_path is not None and not is_excluded(relative_file_path):
                markdown_files.add(file_path)

    return sorted(markdown_files)


def remove_fenced_code_blocks(markdown: str) -> str:
    return re.sub(r"(^|\n)```.*?\n```", "\n", markdown, flags=re.DOTALL)


def iter_link_targets(markdown: str) -> list[str]:
    content = remove_fenced_code_blocks(markdown)
    targets: list[str] = []
    for pattern in (INLINE_LINK_PATTERN, IMAGE_LINK_PATTERN, REFERENCE_DEFINITION_PATTERN):
        targets.extend(match.group(1).strip() for match in pattern.finditer(content))
    return targets


def strip_markdown_title(target: str) -> str:
    if not target:
        return target
    if target[0] in {'"', "'"}:
        return target
    match = re.match(r"([^\s]+)(?:\s+['\"(].*)?$", target)
    if match:
        return match.group(1)
    return target


def is_local_relative_target(target: str) -> bool:
    if not target or target.startswith("#"):
        return False

    split_target = urlsplit(target)
    if split_target.scheme or split_target.netloc:
        return False

    return True


def resolve_target(markdown_file: Path, target: str, root: Path) -> Path | None:
    cleaned_target = strip_markdown_title(target).strip("<>")
    if not is_local_relative_target(cleaned_target):
        return None

    split_target = urlsplit(cleaned_target)
    target_path = unquote(split_target.path)
    if not target_path:
        return None

    if Path(target_path).is_absolute():
        resolved_path = root / target_path.lstrip("/")
    else:
        resolved_path = markdown_file.parent / target_path

    relative_resolved_path = normalized_relative_path(resolved_path, root)
    if relative_resolved_path is None:
        return None

    return resolved_path


def main() -> int:
    arguments = parse_arguments()
    root = arguments.root.resolve()
    failures: list[str] = []

    for markdown_file in iter_markdown_files(root):
        markdown = markdown_file.read_text(encoding="utf-8")
        relative_markdown_file = normalized_relative_path(markdown_file, root) or markdown_file
        for target in iter_link_targets(markdown):
            resolved_target = resolve_target(markdown_file, target, root)
            if resolved_target is None:
                continue
            if not resolved_target.exists():
                failures.append(f"{relative_markdown_file}: broken local Markdown link: {target}")

    if failures:
        print("Broken local Markdown links found:", file=sys.stderr)
        for failure in failures:
            print(f"- {failure}", file=sys.stderr)
        return 1

    print("Local Markdown links are valid.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
