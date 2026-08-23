#!/usr/bin/env python3
"""Validate every GameWIP-owned JSON registry against its draft 2020-12 schema."""

from __future__ import annotations

import json
import re
from pathlib import Path
from typing import Any
from urllib.parse import urlparse

ROOT = Path(__file__).resolve().parents[2]
REGISTRIES = {
    ROOT / "scripts/config/project.json": ROOT / "scripts/schemas/project.schema.json",
    ROOT / "scripts/config/commands.json": ROOT / "scripts/schemas/commands.schema.json",
    ROOT / "scripts/config/project-tools.json": ROOT / "scripts/schemas/project-tools.schema.json",
    ROOT / "scripts/setup/config/setup.json": ROOT / "scripts/schemas/setup.schema.json",
    ROOT / "scripts/setup/config/editors.json": ROOT / "scripts/schemas/editors.schema.json",
}


class ValidationError(ValueError):
    """Describe one registry value that violates its owning schema."""


def resolve_reference(root_schema: dict[str, Any], reference: str) -> dict[str, Any]:
    """Resolve a local JSON Pointer reference used by repository schemas."""
    if not reference.startswith("#/"):
        raise ValidationError(f"Unsupported non-local schema reference: {reference}")
    value: Any = root_schema
    for segment in reference[2:].split("/"):
        value = value[segment.replace("~1", "/").replace("~0", "~")]
    return value


def matches(value: Any, schema: dict[str, Any], root_schema: dict[str, Any]) -> bool:
    """Return whether a conditional subschema accepts a value."""
    try:
        validate(value, schema, root_schema, "$condition")
    except ValidationError:
        return False
    return True


def validate(value: Any, schema: dict[str, Any], root_schema: dict[str, Any], path: str) -> None:
    """Validate the draft 2020-12 keywords used by GameWIP schemas."""
    if "$ref" in schema:
        validate(value, resolve_reference(root_schema, schema["$ref"]), root_schema, path)
        return
    for part in schema.get("allOf", []):
        if "if" not in part or matches(value, part["if"], root_schema):
            validate(value, part.get("then", part), root_schema, path)
    if "const" in schema and value != schema["const"]:
        raise ValidationError(f"{path}: expected constant {schema['const']!r}")
    if "enum" in schema and value not in schema["enum"]:
        raise ValidationError(f"{path}: unsupported value {value!r}")

    expected_type = schema.get("type")
    type_matches = {
        "object": isinstance(value, dict),
        "array": isinstance(value, list),
        "string": isinstance(value, str),
        "boolean": isinstance(value, bool),
        "integer": isinstance(value, int) and not isinstance(value, bool),
    }
    if expected_type and not type_matches.get(expected_type, True):
        raise ValidationError(f"{path}: expected JSON type {expected_type}")

    if isinstance(value, dict):
        properties = schema.get("properties", {})
        for required in schema.get("required", []):
            if required not in value:
                raise ValidationError(f"{path}: missing required property {required!r}")
        for key, child in value.items():
            if key in properties:
                validate(child, properties[key], root_schema, f"{path}.{key}")
            elif schema.get("additionalProperties") is False:
                raise ValidationError(f"{path}: unknown property {key!r}")
            elif isinstance(schema.get("additionalProperties"), dict):
                validate(child, schema["additionalProperties"], root_schema, f"{path}.{key}")
    elif isinstance(value, list):
        if len(value) < schema.get("minItems", 0):
            raise ValidationError(f"{path}: too few items")
        if schema.get("uniqueItems") and len({json.dumps(item, sort_keys=True) for item in value}) != len(value):
            raise ValidationError(f"{path}: duplicate items")
        for index, child in enumerate(value):
            if "items" in schema:
                validate(child, schema["items"], root_schema, f"{path}[{index}]")
    elif isinstance(value, str):
        if len(value) < schema.get("minLength", 0) or len(value) > schema.get("maxLength", len(value)):
            raise ValidationError(f"{path}: invalid string length")
        if "pattern" in schema and re.search(schema["pattern"], value) is None:
            raise ValidationError(f"{path}: value does not match {schema['pattern']!r}")
        if schema.get("format") == "uri" and not urlparse(value).scheme:
            raise ValidationError(f"{path}: expected an absolute URI")
    if "minimum" in schema and value < schema["minimum"]:
        raise ValidationError(f"{path}: value is below its minimum")


def main() -> int:
    """Validate all registry/schema pairs and print an auditable summary."""
    for registry_path, schema_path in REGISTRIES.items():
        registry = json.loads(registry_path.read_text(encoding="utf-8-sig"))
        schema = json.loads(schema_path.read_text(encoding="utf-8-sig"))
        if schema.get("$schema") != "https://json-schema.org/draft/2020-12/schema":
            raise ValidationError(f"{schema_path}: schema must declare draft 2020-12")
        validate(registry, schema, schema, "$")
        print(f"Validated {registry_path.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
