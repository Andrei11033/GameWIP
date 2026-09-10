"""Protect advisory compilation from command quoting and output side effects."""

from __future__ import annotations

import importlib.util
import json
import os
import shlex
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

RUNNER = Path(__file__).resolve().parents[2] / "scripts/lib/hygiene_compiler_warnings.py"
SPEC = importlib.util.spec_from_file_location("hygiene_compiler_warnings", RUNNER)
assert SPEC is not None and SPEC.loader is not None
HYGIENE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(HYGIENE)


class CompilerCommandTests(unittest.TestCase):
    def test_quoted_command_preserves_paths_and_definitions(self) -> None:
        arguments = [
            "C:/Compiler Tools/clang++",
            "-ID:/Game WIP/include",
            '-DTITLE="two words"',
            "-c",
            "D:/Game WIP/source.cpp",
            "-o",
            "D:/Game WIP/source.obj",
        ]
        command = subprocess.list2cmdline(arguments) if os.name == "nt" else shlex.join(arguments)
        expected = [*arguments[:3], arguments[4], "-fsyntax-only", "-Wno-error", "-Wunreachable-code"]
        self.assertEqual(HYGIENE.command_arguments({"command": command}, ["-Wunreachable-code"]), expected)
        self.assertEqual(HYGIENE.command_arguments({"arguments": arguments}, ["-Wunreachable-code"]), expected)

    @unittest.skipUnless(os.name == "nt", "Windows compilation database quoting")
    def test_partially_quoted_windows_arguments(self) -> None:
        command = r'"C:\Compiler Tools\clang++.exe" -I"D:\Game WIP\include" -c "D:\Game WIP\source.cpp"'
        self.assertEqual(
            HYGIENE.split_command(command),
            [r"C:\Compiler Tools\clang++.exe", r"-ID:\Game WIP\include", "-c", r"D:\Game WIP\source.cpp"],
        )

    def test_dependency_outputs_and_specific_warning_errors_are_removed(self) -> None:
        entry = {
            "arguments": [
                "clang++",
                "-MMD",
                "-MF",
                "source.d",
                "-MTsource.obj",
                "-MP",
                "-Werror",
                "-Werror=unused-variable",
                "-Iinclude",
                "-o",
                "source.obj",
                "-c",
                "source.cpp",
            ]
        }
        self.assertEqual(
            HYGIENE.command_arguments(entry, ["-Wunreachable-code"]),
            ["clang++", "-Iinclude", "source.cpp", "-fsyntax-only", "-Wno-error", "-Wunreachable-code"],
        )

    def test_unmatched_filter_fails_instead_of_reporting_a_clean_audit(self) -> None:
        with tempfile.TemporaryDirectory(prefix="gamewip-hygiene-") as directory:
            database = Path(directory) / "compile_commands.json"
            database.write_text(json.dumps([{"file": "external/unused.cpp"}]), encoding="utf-8")
            result = subprocess.run(
                [sys.executable, str(RUNNER), "--database", str(database), "--source-filter", "^game/", "--flag=-Wunreachable-code"],
                check=False,
                capture_output=True,
                text=True,
            )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("no translation units match", result.stderr)


if __name__ == "__main__":
    unittest.main()
