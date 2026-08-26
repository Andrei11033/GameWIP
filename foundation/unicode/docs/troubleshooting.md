@page unicode_troubleshooting Troubleshooting

Unicode rejects malformed text instead of guessing how to repair it. Start
with the reported outcome below, then decide whether the caller needs more
input, a corrected boundary, or a different operation.

## UTF-8 decoding reports `Incomplete`

The supplied bytes form a valid prefix of a longer UTF-8 scalar, such as a valid lead byte without all required continuation bytes.

For streaming input, retain the incomplete bytes and provide more input according to the owning stream policy. For a complete message or file that is
expected to end at this point, treat the input as truncated. Unicode does not insert a replacement character automatically.

## UTF-8 decoding reports `InvalidEncoding`

The leading sequence is malformed. Examples include an invalid lead byte, invalid continuation byte, overlong encoding, encoded surrogate, or value
above `U+10FFFF`.

Appending more bytes cannot repair that leading sequence. The owning component decides whether to reject, replace, log, or otherwise recover.

## Conversion returns `DestinationTooSmall`

The destination cannot hold the next complete encoded scalar. The result reports source and destination progress only through the previous complete
scalar.

Use the corresponding measurement function for exact storage, enlarge the destination, or resume from the reported completed source offset according
to caller policy. No partial surrogate pair or UTF-8 sequence was written.

## Conversion returns `OverlappingRanges`

The source and destination memory ranges overlap. Cross-encoding conversion deliberately rejects overlap before any output is written.

Use separate source and destination storage.

## A boundary call returns `InvalidOffset`

The supplied offset is greater than `text.size()` or points to a UTF-8 continuation byte rather than a code-point boundary.

Offsets may equal `text.size()`. Grapheme traversal does not require the offset itself to be a grapheme boundary; it requires only code-point
alignment.

## Grapheme traversal returns `InvalidEncoding`

Malformed or incomplete UTF-8 was encountered while establishing the requested boundary. Context-sensitive grapheme rules may require the
implementation to inspect text before the supplied offset.

Validate or repair the complete owning text according to component policy before retrying. Unicode does not segment through replacement-character
recovery.

## `GraphemeCursor::reset()` returns `DestinationTooSmall`

The caller-provided boundary span cannot hold every grapheme boundary, including offset 0 and the final `text.size()` boundary. Resize caller storage
to `requiredBoundaryCount` entries and call `reset()` again. The cursor remains unready until a complete valid index fits.

## Repeated grapheme traversal is unexpectedly slow

The stateless `nextGraphemeBoundary()` and `previousGraphemeBoundary()` functions optimize ordinary one-off queries by restarting from a nearby
boundary when that break is provable without earlier state, but Unicode rules such as Indic conjuncts, emoji ZWJ sequences, and regional-indicator
pairing can still require longer lookbehind.

For repeated movement through one segmentation, use `Utf8::GraphemeCursor`. For suffix deletion, move backward, truncate the caller-owned text exactly
at the returned boundary, and call `discardAfterCurrent()`. Arbitrary insertion, replacement, normalization, or non-suffix deletion can change nearby
boundaries and requires re-indexing.

## Official grapheme conformance is skipped

`GraphemeBreakTest.txt` is not available in the normal Unicode data cache and no explicit fixture path was provided.

Prepare the pinned data:

```powershell
.\gamewip.bat unicode verify
```

Then rerun the Unicode module. For CI/release-style validation, set `GAMEWIP_REQUIRE_UNICODE_CONFORMANCE_TESTS=1` so missing conformance data is a
failure rather than a skip.

## Unicode verification reports a generated-table mismatch

Official pinned Unicode inputs produced different formatted bytes from the tracked `unicode_properties.h`.

Do not dismiss the mismatch as formatting noise. The helper formats the generated candidate with the repository `.clang-format` before comparison.
Inspect the retained candidate, verify the pinned version/source configuration, and regenerate intentionally only after understanding the difference.

## Unicode maintenance cannot find Python or clang-format

The maintenance workflow expects the GameWIP MSYS2 UCRT64 Python and clang-format tools. Run:

```powershell
.\gamewip.bat doctor
.\setup.bat repair
```

Controlled environments may override them with `-PythonPath` / `GAMEWIP_PYTHON` and `-ClangFormatPath` / `GAMEWIP_CLANG_FORMAT`.

Ordinary compilation and installed consumers do not require either tool.

## A Unicode version update changes grapheme boundaries

That can be a legitimate behavior change in a newer Unicode Standard. Confirm that every pinned version location and official conformance fixture was
intentionally updated, review the generated diff and Unicode release changes, and run the complete matrix in @ref unicode_testing.

Do not combine generated data or conformance fixtures from different Unicode versions.

## Terminal width behavior is missing

This is intentional. An extended grapheme cluster is not a terminal-cell-width policy.

Terminal owns width, cursor movement, redraw, editing, resize, and line-discipline behavior. Unicode provides reusable scalar and grapheme boundaries
without defining how many terminal cells a cluster occupies.
