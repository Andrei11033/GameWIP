#!/usr/bin/env python3
"""Regression tests for repository-maintenance source policy helpers."""

from __future__ import annotations

import importlib.util
import tempfile
import unittest
from pathlib import Path

SCRIPT = Path(__file__).with_name("check_repository_standards.py")
SPEC = importlib.util.spec_from_file_location("check_repository_standards", SCRIPT)
assert SPEC is not None and SPEC.loader is not None
CHECKER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CHECKER)


class UnicodeAuthorityTests(unittest.TestCase):
    """Keep the direct-converter policy covered without polluting maintained source."""

    def check_fixture(self, relative: str, contents: str) -> list[str]:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            fixture = root / relative
            fixture.parent.mkdir(parents=True, exist_ok=True)
            fixture.write_text(contents, encoding="utf-8")
            previous_root = CHECKER.ROOT
            CHECKER.ROOT = root
            try:
                failures: list[str] = []
                CHECKER.check_unicode_conversion_authority(failures)
                return failures
            finally:
                CHECKER.ROOT = previous_root

    def test_direct_multibyte_converter_is_rejected(self) -> None:
        failures = self.check_fixture("fixture.cpp", "auto value = MultiByteToWideChar();\n")
        self.assertEqual(len(failures), 1)
        self.assertIn("fixture.cpp:1:", failures[0])

    def test_direct_wide_converter_is_rejected(self) -> None:
        failures = self.check_fixture("fixture.cpp", "auto value = WideCharToMultiByte();\n")
        self.assertEqual(len(failures), 1)

    def test_ipp_is_checked_for_direct_converters(self) -> None:
        failures = self.check_fixture("fixture.ipp", "auto value = MultiByteToWideChar();\n")
        self.assertEqual(len(failures), 1)

    def test_codecvt_family_is_rejected_case_sensitively(self) -> None:
        failures = self.check_fixture("fixture.hpp", "std::wstring_convert converter;\nstd::codecvt facet;\n")
        self.assertEqual(len(failures), 2)

    def test_c_runtime_conversion_family_is_rejected(self) -> None:
        contents = "\n".join(
            f"auto value{index} = {name}();" for index, name in enumerate(("mbstowcs", "wcstombs", "mbrtoc16", "c16rtomb", "mbrtoc32", "c32rtomb"))
        )
        self.assertEqual(len(self.check_fixture("fixture.cpp", contents)), 6)

    def test_policy_is_case_sensitive(self) -> None:
        self.assertEqual(self.check_fixture("fixture.cpp", "std::CODECVT facet;\nmultibytetowidechar();\n"), [])

    def test_digit_separators_do_not_hide_later_converter(self) -> None:
        failures = self.check_fixture("fixture.cpp", "auto value = 1'000;\nMultiByteToWideChar(...);\n")
        self.assertEqual(len(failures), 1)
        self.assertIn("fixture.cpp:2:", failures[0])

    def test_hex_digit_separators_do_not_hide_later_converter(self) -> None:
        failures = self.check_fixture("fixture.cpp", "auto value = 0xAB'CD;\nMultiByteToWideChar(...);\n")
        self.assertEqual(len(failures), 1)
        self.assertIn("fixture.cpp:2:", failures[0])

    def test_codecvt_spacing_with_comments_is_rejected(self) -> None:
        failures = self.check_fixture("fixture.cpp", "std /* comment */ :: codecvt_utf8<char32_t> facet;\n")
        self.assertEqual(len(failures), 1)

    def test_character_literals_do_not_corrupt_scanning(self) -> None:
        contents = "wchar_t value = L'x';\nconst char escaped = '\\'';\nMultiByteToWideChar(...);\n"
        failures = self.check_fixture("fixture.cpp", contents)
        self.assertEqual(len(failures), 1)
        self.assertIn("fixture.cpp:3:", failures[0])

    def test_comments_and_literals_are_ignored(self) -> None:
        contents = (
            "// MultiByteToWideChar() and std::codecvt are policy examples.\n"
            "/* WideCharToMultiByte() */\n"
            'const char *text = "don\'t mention MultiByteToWideChar here";\n'
            'const char *raw = R"(raw std::codecvt and wcstombs)";\n'
            "const char marker = '\\'';\n"
        )
        self.assertEqual(self.check_fixture("fixture.cpp", contents), [])

    def test_excluded_trees_are_not_maintained(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "fixture.cpp").write_text("auto value = MultiByteToWideChar();\n", encoding="utf-8")
            for excluded in (".git", "build", "external", "generated", "vendor"):
                path = root / excluded / "fixture.cpp"
                path.parent.mkdir(parents=True)
                path.write_text("auto value = MultiByteToWideChar();\n", encoding="utf-8")
            previous_root = CHECKER.ROOT
            CHECKER.ROOT = root
            try:
                self.assertEqual([path.relative_to(root).as_posix() for path in CHECKER.maintained_files()], ["fixture.cpp"])
            finally:
                CHECKER.ROOT = previous_root

    def test_unicode_implementation_is_also_checked(self) -> None:
        failures = self.check_fixture("foundation/unicode/fixture.cpp", "auto value = MultiByteToWideChar();\n")
        self.assertEqual(len(failures), 1)

    def test_non_cpp_documentation_is_allowed(self) -> None:
        self.assertEqual(self.check_fixture("notes.md", "MultiByteToWideChar is intentionally mentioned here.\n"), [])

    def test_compliant_unicode_usage_passes(self) -> None:
        self.assertEqual(self.check_fixture("fixture.cpp", "return GameWIP::Unicode::Utf8::measureToUtf16(text);\n"), [])


if __name__ == "__main__":
    unittest.main()
