#!/usr/bin/env python3
"""Prove that every maintained first-party file has explicit quality ownership."""

from __future__ import annotations

import json
import subprocess
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
POLICY_PATH = ROOT / "config/quality/file-ownership.json"


def maintained_worktree_files() -> list[Path]:
    result = subprocess.run(
        ["git", "ls-files", "--cached", "--others", "--exclude-standard", "-z"],
        cwd=ROOT,
        check=True,
        capture_output=True,
    )
    return [ROOT / item.decode("utf-8") for item in result.stdout.split(b"\0") if item]


def relative(path: Path) -> str:
    return path.relative_to(ROOT).as_posix()


def classify(path: Path, policies: list[dict[str, object]]) -> dict[str, object] | None:
    name = path.name
    suffix = relative(path)
    extension = path.suffix.lower()
    # Most-specific ownership wins: exact name -> longest special suffix -> extension.
    exact = [policy for policy in policies if name in policy.get("exactNames", [])]
    if exact:
        return exact[0] if len(exact) == 1 else None
    special = [
        (len(candidate), policy)
        for policy in policies
        for candidate in policy.get("suffixes", [])
        if suffix.lower().endswith(str(candidate).lower())
    ]
    if special:
        special.sort(key=lambda item: item[0], reverse=True)
        longest = special[0][0]
        matches = [policy for length, policy in special if length == longest]
        return matches[0] if len(matches) == 1 else None
    matches = [policy for policy in policies if extension in {str(item).lower() for item in policy.get("extensions", [])}]
    return matches[0] if len(matches) == 1 else None


def generic_text_failures(path: Path, allow_trailing: bool) -> list[str]:
    data = path.read_bytes()
    failures: list[str] = []
    if b"\0" in data:
        return ["contains NUL bytes but is classified as maintained text"]
    if data.startswith(b"\xef\xbb\xbf"):
        failures.append("contains a UTF-8 BOM; repository text is UTF-8 without BOM")
    try:
        text = data.decode("utf-8")
    except UnicodeDecodeError as error:
        return [f"is not valid UTF-8: {error}"]
    if "\r" in text:
        failures.append("contains CR/CRLF; maintained text must use LF")
    if text and not text.endswith("\n"):
        failures.append("does not end with a final newline")
    if not allow_trailing:
        for number, line in enumerate(text.splitlines(), 1):
            if line.rstrip(" \t") != line:
                failures.append(f"line {number} has trailing whitespace")
                break
    return failures


def owner_syntax_failures(path: Path, policy_id: str) -> list[str]:
    try:
        text = path.read_text(encoding="utf-8")
        if policy_id == "special-json" or (policy_id == "json" and path.suffix.lower() == ".json"):
            json.loads(text)
        elif policy_id == "xml-manifest":
            ET.fromstring(text)
    except (UnicodeDecodeError, json.JSONDecodeError, ET.ParseError) as error:
        return [f"is not valid {policy_id} syntax: {error}"]
    return []


def main() -> int:
    config = json.loads(POLICY_PATH.read_text(encoding="utf-8"))
    excludes = tuple(config["excludePrefixes"])
    historical = tuple(config["historicalPrefixes"])
    binary = {item.lower() for item in config["binaryExtensions"]}
    policies = config["policies"]
    failures: list[str] = []
    covered = 0
    intentional_no_format = 0
    binary_count = 0

    for path in maintained_worktree_files():
        rel = relative(path)
        if rel.startswith(excludes) or rel.startswith(historical) or not path.is_file():
            continue
        if path.suffix.lower() in binary:
            binary_count += 1
            continue
        policy = classify(path, policies)
        if policy is None:
            failures.append(f"{rel}: no unique maintained-file quality owner")
            continue
        covered += 1
        if policy["formatter"] is None:
            intentional_no_format += 1
        for failure in generic_text_failures(path, bool(policy["allowTrailingWhitespace"])):
            failures.append(f"{rel}: {failure}")
        for failure in owner_syntax_failures(path, str(policy["id"])):
            failures.append(f"{rel}: {failure}")

    print(f"Quality ownership: covered={covered} intentional-no-format={intentional_no_format} binary={binary_count} failures={len(failures)}")
    if failures:
        for failure in failures:
            print(f"  - {failure}")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
