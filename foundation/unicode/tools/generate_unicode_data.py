#!/usr/bin/env python3
"""Generate GameWIP's compact Unicode 17 grapheme-property trie.

The script consumes an unpacked Unicode Character Database directory. It never
runs as part of an ordinary configure, build, test, package, or consumer flow.
"""

from __future__ import annotations

import argparse
from collections.abc import Iterable, Iterator
from dataclasses import dataclass
from pathlib import Path

UNICODE_VERSION = "17.0.0"
EMOJI_VERSION = "17.0"
MAX_CODE_POINT = 0x10FFFF
CODE_POINT_COUNT = MAX_CODE_POINT + 1
MIN_BLOCK_SHIFT = 5
MAX_BLOCK_SHIFT = 9

GCB_VALUES = {
    "Other": 0,
    "CR": 1,
    "LF": 2,
    "Control": 3,
    "Extend": 4,
    "ZWJ": 5,
    "Regional_Indicator": 6,
    "Prepend": 7,
    "SpacingMark": 8,
    "L": 9,
    "V": 10,
    "T": 11,
    "LV": 12,
    "LVT": 13,
}

INCB_VALUES = {
    "None": 0,
    "Consonant": 1,
    "Extend": 2,
    "Linker": 3,
}

GCB_MASK = 0x0F
INCB_MASK = 0x30
EXTENDED_PICTOGRAPHIC_MASK = 0x40


@dataclass(frozen=True)
class SourcePaths:
    grapheme_break: Path
    derived_core: Path
    emoji_data: Path


@dataclass(frozen=True)
class GeneratedTrie:
    block_shift: int
    high_start: int
    indexes: tuple[int, ...]
    blocks: tuple[bytes, ...]
    index_type: str


def parse_arguments() -> argparse.Namespace:
    script_directory = Path(__file__).resolve().parent
    default_output = script_directory.parent / "internal" / "generated" / "unicode_properties.h"

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--ucd-dir",
        type=Path,
        required=True,
        help="Path to the unpacked Unicode 17.0.0 ucd directory.",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=default_output,
        help=f"Generated header path (default: {default_output}).",
    )
    return parser.parse_args()


def source_paths(ucd_directory: Path) -> SourcePaths:
    return SourcePaths(
        grapheme_break=ucd_directory / "auxiliary" / "GraphemeBreakProperty.txt",
        derived_core=ucd_directory / "DerivedCoreProperties.txt",
        emoji_data=ucd_directory / "emoji" / "emoji-data.txt",
    )


def require_source(path: Path, accepted_version_markers: Iterable[str]) -> None:
    if not path.is_file():
        raise FileNotFoundError(f"required Unicode data file is missing: {path}")

    header = "\n".join(path.read_text(encoding="utf-8").splitlines()[:40])
    if not any(marker in header for marker in accepted_version_markers):
        markers = ", ".join(repr(marker) for marker in accepted_version_markers)
        raise ValueError(f"{path} does not declare the expected version marker ({markers})")


def records(path: Path) -> Iterator[tuple[str, ...]]:
    with path.open("r", encoding="utf-8") as source:
        for line_number, line in enumerate(source, start=1):
            payload = line.split("#", maxsplit=1)[0].strip()
            if not payload:
                continue

            fields = tuple(field.strip() for field in payload.split(";"))
            if len(fields) < 2:
                raise ValueError(f"malformed record at {path}:{line_number}: {line.rstrip()}")
            yield fields


def code_points(field: str) -> range:
    if ".." in field:
        first_text, last_text = field.split("..", maxsplit=1)
        first = int(first_text, 16)
        last = int(last_text, 16)
    else:
        first = int(field, 16)
        last = first

    if first < 0 or last > MAX_CODE_POINT or first > last:
        raise ValueError(f"invalid Unicode range: {field}")
    return range(first, last + 1)


def apply_grapheme_break(data: bytearray, path: Path) -> None:
    for fields in records(path):
        property_name = fields[1]
        if property_name not in GCB_VALUES:
            raise ValueError(f"unsupported Grapheme_Cluster_Break value {property_name!r} in {path}")

        packed_value = GCB_VALUES[property_name]
        for code_point in code_points(fields[0]):
            data[code_point] = (data[code_point] & ~GCB_MASK) | packed_value


def apply_indic_conjunct_break(data: bytearray, path: Path) -> None:
    for fields in records(path):
        if fields[1] != "InCB":
            continue
        if len(fields) < 3:
            raise ValueError(f"missing Indic_Conjunct_Break value in {path}: {'; '.join(fields)}")

        property_name = fields[2]
        if property_name not in INCB_VALUES:
            raise ValueError(f"unsupported Indic_Conjunct_Break value {property_name!r} in {path}")

        packed_value = INCB_VALUES[property_name] << 4
        for code_point in code_points(fields[0]):
            data[code_point] = (data[code_point] & ~INCB_MASK) | packed_value


def apply_extended_pictographic(data: bytearray, path: Path) -> None:
    for fields in records(path):
        if fields[1] != "Extended_Pictographic":
            continue
        for code_point in code_points(fields[0]):
            data[code_point] |= EXTENDED_PICTOGRAPHIC_MASK


def remove_algorithmic_ranges(data: bytearray) -> None:
    # ASCII CR/LF/control classification and precomposed Hangul LV/LVT
    # classification are cheaper to compute directly than to store in the trie.
    data[0x0000:0x0080] = bytes(0x80)
    data[0xAC00:0xD7A4] = bytes(0xD7A4 - 0xAC00)


def build_trie(data: bytearray, block_shift: int) -> GeneratedTrie:
    block_size = 1 << block_shift
    block_mask = block_size - 1
    last_non_default = max(index for index, value in enumerate(data) if value != 0)
    high_start = (last_non_default + block_size) & ~block_mask

    unique_blocks: list[bytes] = []
    block_ids: dict[bytes, int] = {}
    indexes: list[int] = []

    for block_start in range(0, high_start, block_size):
        block = bytes(data[block_start : block_start + block_size])
        block_id = block_ids.get(block)
        if block_id is None:
            block_id = len(unique_blocks)
            block_ids[block] = block_id
            unique_blocks.append(block)
        indexes.append(block_id)

    if len(unique_blocks) <= 0x100:
        index_type = "std::uint8_t"
    elif len(unique_blocks) <= 0x10000:
        index_type = "std::uint16_t"
    else:
        index_type = "std::uint32_t"

    return GeneratedTrie(
        block_shift=block_shift,
        high_start=high_start,
        indexes=tuple(indexes),
        blocks=tuple(unique_blocks),
        index_type=index_type,
    )


def index_width(trie: GeneratedTrie) -> int:
    return {"std::uint8_t": 1, "std::uint16_t": 2, "std::uint32_t": 4}[trie.index_type]


def table_size(trie: GeneratedTrie) -> int:
    block_size = 1 << trie.block_shift
    return len(trie.indexes) * index_width(trie) + len(trie.blocks) * block_size


def select_trie(data: bytearray) -> tuple[GeneratedTrie, tuple[GeneratedTrie, ...]]:
    candidates = tuple(build_trie(data, shift) for shift in range(MIN_BLOCK_SHIFT, MAX_BLOCK_SHIFT + 1))
    # Prefer the smaller block when table sizes tie because it has finer cache locality.
    return min(candidates, key=lambda trie: (table_size(trie), trie.block_shift)), candidates


def formatted_values(values: Iterable[int], per_line: int, hexadecimal: bool = False) -> str:
    rendered = [f"0x{value:02X}" if hexadecimal else str(value) for value in values]
    lines = []
    for start in range(0, len(rendered), per_line):
        lines.append("        " + ", ".join(rendered[start : start + per_line]) + ",")
    return "\n".join(lines)


def render_header(trie: GeneratedTrie) -> str:
    block_size = 1 << trie.block_shift
    block_mask = block_size - 1
    flattened_blocks = tuple(value for block in trie.blocks for value in block)
    return f"""/// @file unicode_properties.h
/// @brief Generated Unicode {UNICODE_VERSION} grapheme-property trie. Do not edit manually.
/// @details Derived from pinned Unicode {UNICODE_VERSION} data files.
/// Unicode data license: Unicode License v3 (SPDX Unicode-3.0), https://www.unicode.org/license.txt

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace GameWIP::Unicode::Internal::Generated
{{
    inline constexpr std::uint8_t kUnicodeVersionMajor = 17;
    inline constexpr std::uint8_t kUnicodeVersionMinor = 0;
    inline constexpr std::uint8_t kUnicodeVersionPatch = 0;

    inline constexpr std::size_t kBlockShift = {trie.block_shift};
    inline constexpr std::size_t kBlockSize = {block_size};
    inline constexpr std::size_t kBlockMask = {block_mask};
    inline constexpr char32_t kHighStart = static_cast<char32_t>(0x{trie.high_start:X});

    using BlockIndex = {trie.index_type};

    alignas(64) inline constexpr std::array<BlockIndex, {len(trie.indexes)}> kBlockIndexes{{
{formatted_values(trie.indexes, 16)}
    }};

    alignas(64) inline constexpr std::array<std::uint8_t, {len(flattened_blocks)}> kPropertyBlocks{{
{formatted_values(flattened_blocks, 16, hexadecimal=True)}
    }};
}} // namespace GameWIP::Unicode::Internal::Generated
"""


def generate(paths: SourcePaths, output: Path) -> tuple[GeneratedTrie, tuple[GeneratedTrie, ...]]:
    require_source(paths.grapheme_break, (UNICODE_VERSION,))
    require_source(paths.derived_core, (UNICODE_VERSION,))
    require_source(paths.emoji_data, (UNICODE_VERSION, f"Version: {EMOJI_VERSION}"))

    data = bytearray(CODE_POINT_COUNT)
    apply_grapheme_break(data, paths.grapheme_break)
    apply_indic_conjunct_break(data, paths.derived_core)
    apply_extended_pictographic(data, paths.emoji_data)
    remove_algorithmic_ranges(data)

    trie, candidates = select_trie(data)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(render_header(trie), encoding="utf-8", newline="\n")
    return trie, candidates


def main() -> None:
    arguments = parse_arguments()
    paths = source_paths(arguments.ucd_dir.resolve())
    trie, candidates = generate(paths, arguments.output.resolve())

    print(f"{'Unicode version:':<17}{UNICODE_VERSION}")
    print(f"{'High start:':<17}U+{trie.high_start:06X}")
    print()
    print(
        "Block size | Index width | Index bytes | Unique blocks | Block bytes | Total bytes | Selection"
    )
    for candidate in candidates:
        block_size = 1 << candidate.block_shift
        candidate_index_bytes = len(candidate.indexes) * index_width(candidate)
        property_bytes = len(candidate.blocks) * block_size
        selection = "selected" if candidate is trie else ""
        print(
            f"{block_size:10} | {index_width(candidate):11} | {candidate_index_bytes:11} | "
            f"{len(candidate.blocks):13} | {property_bytes:11} | {table_size(candidate):11} | {selection}"
        )
    print()
    print(f"{'Selected block size:':<21}{1 << trie.block_shift}")
    print(f"{'Total table bytes:':<21}{table_size(trie)}")
    print(f"{'Wrote:':<21}{arguments.output.resolve()}")


if __name__ == "__main__":
    main()
