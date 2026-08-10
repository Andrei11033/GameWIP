@page unicode_testing Testing

Unicode validation covers public contracts, generated property data, official grapheme conformance, package boundaries, sanitizers, static analysis, and diagnostic performance benchmarks.

## Common workflow

Prepare or verify the pinned Unicode data, then run the focused correctness module:

```powershell
.\gamewip.bat unicode -UnicodeAction verify
.\gamewip.bat module -Module unicode -BuildIfMissing
```

Run the complete correctness and package suite with:

```powershell
.\gamewip.bat test -Preset test
```

## Correctness coverage

The Unicode module covers:

- Unicode Standard version reporting and scalar/surrogate boundaries.
- Strict UTF-8 decoding, encoding, and whole-range validation.
- Invalid lead and continuation bytes, overlong sequences, encoded surrogates, and values above `U+10FFFF`.
- Every proper split point of every valid multi-byte UTF-8 scalar, with deterministic zero-progress `Incomplete` results.
- Strict UTF-16 BMP, surrogate-pair, dangling-high-surrogate, isolated-low-surrogate, and malformed-pair behavior.
- UTF-8/UTF-16 measurement and conversion, exact and insufficient destinations, completed progress, untouched destination tails, empty input, embedded `U+0000`, and overlapping ranges.
- Exhaustive UTF-8/UTF-16 encode/decode/conversion round trips for all 1,112,064 Unicode scalar values.
- Previous and next UTF-8 code-point boundaries at empty, beginning, middle, end, malformed, incomplete, and misaligned offsets.
- Targeted extended grapheme cases for CR/LF, combining marks, Hangul, Prepend, SpacingMark, Indic conjuncts, emoji ZWJ sequences, regional indicators, and emoji modifiers.
- Caller-backed `Utf8::GraphemeCursor` sizing, indexing, exact seek, forward/backward stepping, endpoint behavior, malformed input, empty input, and suffix-index discard.
- Generated-table shape, packed-value invariants, and representative property lookups.
- Every case in the official Unicode 17.0.0 `GraphemeBreakTest.txt`, checked through both stateless traversal directions and the indexed cursor boundary sequence.

## Official conformance data

The correctness test searches for `GraphemeBreakTest.txt` in this order:

1. `GAMEWIP_UNICODE_GRAPHEME_BREAK_TEST`, when set to an explicit file.
2. `GAMEWIP_UNICODE_DATA_ROOT/17.0.0/ucd/auxiliary/GraphemeBreakTest.txt`.
3. The normal GameWIP cache at `build/unicode-data/17.0.0/ucd/auxiliary/GraphemeBreakTest.txt`.

When the file is absent, an ordinary local test run skips only the official conformance suite. Require the fixture and fail closed with:

```powershell
$env:GAMEWIP_REQUIRE_UNICODE_CONFORMANCE_TESTS = "1"
.\gamewip.bat module -Module unicode -BuildIfMissing
Remove-Item Env:GAMEWIP_REQUIRE_UNICODE_CONFORMANCE_TESTS
```

The primary CI correctness job prepares the pinned data before CTest and requires official Unicode conformance. Ordinary configure, build, test, package, and installed-consumer flows do not perform a Unicode data download.

## Unicode data and version policy

Runtime grapheme segmentation is pinned to Unicode Standard 17.0.0. `getStandardVersion()` exposes that data/behavior version to consumers.

The generated runtime table is derived from these Unicode 17.0.0 data files:

- `ucd/auxiliary/GraphemeBreakProperty.txt` for `Grapheme_Cluster_Break`.
- `ucd/DerivedCoreProperties.txt` for `Indic_Conjunct_Break`.
- `ucd/emoji/emoji-data.txt` for `Extended_Pictographic`.

Conformance uses:

- `ucd/auxiliary/GraphemeBreakTest.txt`.

The Unicode 17.0.0 data files identify Unicode, Inc. as the copyright holder and refer users to the Unicode terms and license. Unicode Data Files are licensed under Unicode License v3 (SPDX `Unicode-3.0`) unless a file states otherwise. See the [Unicode License v3](https://www.unicode.org/license.txt) and [Unicode Terms of Use](https://www.unicode.org/terms_of_use.html).

`foundation/unicode/tools/generate_unicode_data.py` is the deterministic, Python-standard-library-only generator. The generated `unicode/internal/generated/unicode_properties.h` is checked in, so normal consumers do not require Python, Unicode source files, or network access.

The current Unicode 17.0.0 generation reports:

```text
High start: U+0E1000
Index entries: 14400 (14400 bytes)
Unique blocks: 226 (14464 bytes)
Total table bytes: 28864
```

These values are review diagnostics for the current data layout, not public ABI promises.

## Maintenance commands

Inspect the configured version, tools, cache, and tracked generated header:

```powershell
.\gamewip.bat unicode -UnicodeAction status
```

Verify that official pinned inputs reproduce the checked-in table byte-for-byte after repository formatting:

```powershell
.\gamewip.bat unicode -UnicodeAction verify
```

Force a fresh official download before verification:

```powershell
.\gamewip.bat unicode -UnicodeAction verify -RefreshUnicodeData
```

Regenerate intentionally:

```powershell
.\gamewip.bat unicode -UnicodeAction regenerate
git diff -- foundation/unicode/internal/generated/unicode_properties.h
```

`verify` does not modify the tracked table. `regenerate` creates and formats a candidate first, then replaces the tracked header only when its content differs.

## Updating the Unicode version

A Unicode version update is an intentional behavior change rather than a routine table refresh:

1. Update the pinned Unicode/UCD and emoji versions in the generator and GameWIP Unicode maintenance configuration.
2. Update version assertions, conformance-file markers and paths, and manual text that intentionally pins the previous version.
3. Refresh official data and regenerate the table.
4. Review Unicode release changes, the generated diff, high-start value, unique-block count, and total table bytes.
5. Run official grapheme conformance with `GAMEWIP_REQUIRE_UNICODE_CONFORMANCE_TESTS=1`.
6. Run normal correctness/package validation, AddressSanitizer, static analysis/formatting, documentation checks, and benchmark registration.
7. Compare representative `BM_Unicode_*` benchmark results when data or segmentation behavior changes materially.
8. Record compatibility impact when updated Unicode rules or properties change grapheme boundaries.

Do not edit the generated table manually or accept a mismatched verification result.

## Benchmarks

Build benchmark registration and run the Unicode family:

```powershell
.\gamewip.bat benchmark
.\build\benchmark\GameWIPBenchmarks.exe --benchmark_filter=BM_Unicode
```

The Unicode benchmark family covers representative UTF-8 decoding, validation, scalar encoding, code-point traversal, existing stateless grapheme traversal, deep non-ASCII stateless next/previous queries, indexed forward traversal, and repeated grapheme-aware suffix deletion on a long combining/emoji/Indic/regional-indicator fixture. Timings are diagnostic and are never correctness gates; see @ref project_benchmarking.

For the new traversal benchmarks, compare scaling as fixture size grows. The intended behavior is bounded local work for ordinary stateless queries where a nearby safe restart exists, and linear-overall repeated cursor traversal/edit work after one linear index build.

## Final validation

For an implementation, generated-data, test, or manual change, run:

```powershell
.\gamewip.bat format -FormatAction check
.\gamewip.bat unicode -UnicodeAction verify
.\gamewip.bat test -Preset test
.\gamewip.bat asan
.\gamewip.bat analyze
.\gamewip.bat benchmark
.\gamewip.bat docs
```

Documentation standards, Markdown links, and repository checks remain part of the normal validation workflow. See @ref project_static_analysis and @ref project_documentation.
