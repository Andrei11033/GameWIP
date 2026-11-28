"""Test changed-file validation routing."""

from __future__ import annotations

import unittest

from validation_scope import classify


class ValidationScopeTests(unittest.TestCase):
    def test_cpp_source_selects_code_validation(self) -> None:
        scope = classify(["foundation/io/core/file.cpp"], False, "")

        self.assertEqual(scope["scope"], "code")
        self.assertTrue(scope["needs_cpp"])
        self.assertFalse(scope["needs_packages"])
        self.assertFalse(scope["needs_docs"])

    def test_library_header_selects_package_and_documentation_checks(self) -> None:
        scope = classify(["engine/input/system.h"], False, "")

        self.assertTrue(scope["needs_cpp"])
        self.assertTrue(scope["needs_packages"])
        self.assertTrue(scope["needs_docs"])
        self.assertTrue(scope["needs_dependencies"])

    def test_documentation_change_skips_cpp_builds(self) -> None:
        scope = classify(["docs/build.md"], False, "")

        self.assertEqual(scope["scope"], "docs")
        self.assertTrue(scope["needs_docs"])
        self.assertFalse(scope["needs_cpp"])

    def test_unrelated_safe_file_skips_validation(self) -> None:
        scope = classify(["LICENSE"], False, "")

        self.assertEqual(scope["scope"], "empty")
        self.assertFalse(scope["needs_cpp"])
        self.assertFalse(scope["needs_repository_checks"])

    def test_unknown_file_requests_full_validation(self) -> None:
        scope = classify(["assets/controller-icon.png"], False, "")

        self.assertEqual(scope["scope"], "full")
        self.assertTrue(scope["full_validation"])

    def test_validation_workflow_change_requests_full_validation(self) -> None:
        scope = classify([".github/workflows/validation.yml"], False, "")

        self.assertTrue(scope["full_validation"])
        self.assertTrue(scope["needs_cpp"])
        self.assertTrue(scope["needs_actionlint"])

    def test_filesystem_change_selects_symlink_scenarios(self) -> None:
        scope = classify(["foundation/filesystem/core/path.cpp"], False, "")

        self.assertTrue(scope["needs_symlink_tests"])
        self.assertFalse(scope["needs_unicode_data"])

    def test_unicode_change_selects_conformance_data(self) -> None:
        scope = classify(["foundation/unicode/unicode.h"], False, "")

        self.assertTrue(scope["needs_unicode_data"])
        self.assertTrue(scope["needs_docs"])


if __name__ == "__main__":
    unittest.main()
