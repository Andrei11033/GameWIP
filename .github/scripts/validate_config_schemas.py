#!/usr/bin/env python3
"""Validate every GameWIP-owned JSON registry with a conforming Draft 2020-12 implementation."""

from __future__ import annotations

import json
from pathlib import Path

from jsonschema import Draft202012Validator

ROOT = Path(__file__).resolve().parents[2]
REGISTRIES = {
    ROOT / "scripts/config/project.json": ROOT / "scripts/schemas/project.schema.json",
    ROOT / "scripts/config/commands.json": ROOT / "scripts/schemas/commands.schema.json",
    ROOT / "scripts/config/project-tools.json": ROOT / "scripts/schemas/project-tools.schema.json",
    ROOT / "scripts/setup/config/setup.json": ROOT / "scripts/schemas/setup.schema.json",
    ROOT / "scripts/setup/config/editors.json": ROOT / "scripts/schemas/editors.schema.json",
    ROOT / "config/quality/file-ownership.json": ROOT / "scripts/schemas/quality-file-ownership.schema.json",
}


def read_json(path: Path) -> object:
    return json.loads(path.read_text(encoding="utf-8-sig"))


def main() -> int:
    failures: list[str] = []
    for registry_path, schema_path in REGISTRIES.items():
        schema = read_json(schema_path)
        if schema.get("$schema") != "https://json-schema.org/draft/2020-12/schema":
            failures.append(f"{schema_path.relative_to(ROOT)}: schema must declare Draft 2020-12")
            continue
        Draft202012Validator.check_schema(schema)
        validator = Draft202012Validator(schema)
        errors = sorted(validator.iter_errors(read_json(registry_path)), key=lambda error: list(error.absolute_path))
        if errors:
            for error in errors:
                location = ".".join(str(part) for part in error.absolute_path) or "$"
                failures.append(f"{registry_path.relative_to(ROOT)}:{location}: {error.message}")
        else:
            print(f"Validated {registry_path.relative_to(ROOT)}")
    if failures:
        print("Configuration schema validation failed:")
        for failure in failures:
            print(f"  - {failure}")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
