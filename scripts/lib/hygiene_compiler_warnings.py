#!/usr/bin/env python3
"""Run advisory compiler warnings from the analyze compilation database."""

from __future__ import annotations

import argparse
import ctypes
import json
import os
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


def split_command(command: str) -> list[str]:
    """Decode the host shell's quoting without executing the compiler command."""
    if os.name != "nt":
        return shlex.split(command)

    # CMake's Windows commands can quote only part of an argument, such as an
    # include path. shlex's non-POSIX mode keeps those quotes and splits spaces.
    shell = ctypes.WinDLL("shell32", use_last_error=True)
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    parse = shell.CommandLineToArgvW
    parse.argtypes = [ctypes.c_wchar_p, ctypes.POINTER(ctypes.c_int)]
    parse.restype = ctypes.POINTER(ctypes.c_wchar_p)
    kernel.LocalFree.argtypes = [ctypes.c_void_p]
    kernel.LocalFree.restype = ctypes.c_void_p

    count = ctypes.c_int()
    parsed = parse(command, ctypes.byref(count))
    if not parsed:
        raise ctypes.WinError(ctypes.get_last_error())
    try:
        return list(parsed[: count.value])
    finally:
        kernel.LocalFree(parsed)


def command_arguments(entry: dict[str, object], flags: list[str]) -> list[str]:
    command = entry.get("arguments")
    arguments = [str(value) for value in command] if isinstance(command, list) else split_command(str(entry["command"]))
    if not arguments:
        raise ValueError("empty compiler command")

    # Reuse the configured compile command's include paths and definitions while
    # preventing this advisory pass from writing object files or treating any
    # unrelated warning as a hard failure.
    filtered: list[str] = [arguments[0]]
    index = 1
    while index < len(arguments):
        argument = arguments[index]
        if argument in {"-o", "-MF", "-MT", "-MQ"} and index + 1 < len(arguments):
            index += 2
            continue
        if argument in {"-c", "-MD", "-MMD", "-MP", "-MG", "-M", "-MM", "-Werror"} or argument.startswith(("-Werror=", "-MF", "-MT", "-MQ")):
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
    if not selected_entries:
        print("compiler-warning provider failed: no translation units match the source filter", file=sys.stderr)
        return 1

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
        except (OSError, ValueError, KeyError) as error:
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
