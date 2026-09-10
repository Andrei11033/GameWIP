#!/usr/bin/env python3
"""Run advisory compiler warnings from the analyze compilation database."""

from __future__ import annotations

import argparse
import json
import re
import shlex
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--database", required=True, type=Path)
    parser.add_argument("--source-filter", required=True)
    parser.add_argument("--flag", action="append", required=True)
    return parser.parse_args()


def command_arguments(entry: dict[str, object], flags: list[str]) -> list[str]:
    command = entry.get("arguments")
    arguments = [str(value) for value in command] if isinstance(command, list) else shlex.split(str(entry["command"]), posix=False)

    # Reuse the configured compile command's include paths and definitions while
    # preventing this advisory pass from writing object files or treating any
    # unrelated warning as a hard failure.
    filtered: list[str] = [arguments[0]]
    index = 1
    while index < len(arguments):
        argument = arguments[index]
        if argument == "-o" and index + 1 < len(arguments):
            index += 2
            continue
        if argument == "-c":
            index += 1
            continue
        filtered.append(argument)
        index += 1
    filtered.extend(("-fsyntax-only", "-Wno-error", *flags))
    return filtered


def main() -> int:
    options = parse_arguments()
    source_pattern = re.compile(options.source_filter)
    entries = json.loads(options.database.read_text(encoding="utf-8"))
    selected_entries = [entry for entry in entries if source_pattern.search(str(entry.get("file", "")).replace("\\", "/"))]

    def run_entry(entry: dict[str, object]) -> tuple[str, int, str]:
        source = str(entry.get("file", "")).replace("\\", "/")
        directory = Path(str(entry["directory"]))
        try:
            result = subprocess.run(
                command_arguments(entry, options.flag),
                cwd=directory,
                check=False,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
            )
        except OSError as error:
            return "", 1, f"compiler-warning provider failed for {source}: {error}\n"
        return result.stdout or "", result.returncode, ""

    failures = 0
    # Translation units are independent. Preserve database order when emitting
    # diagnostics so repeated audits produce stable evidence despite parallel work.
    with ThreadPoolExecutor(max_workers=min(4, max(1, len(selected_entries)))) as executor:
        results = executor.map(run_entry, selected_entries)
        for output, returncode, error in results:
            if output:
                print(output, end="")
            if error:
                print(error, file=sys.stderr, end="")
            failures += int(returncode != 0)

    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
