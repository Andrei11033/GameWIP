@page terminal_unicode_io Unicode and byte I/O

Terminal presents UTF-8 text at its public boundary and keeps a separate raw-byte path. UTF-8 rules (strictness, `EncodingFailed`, no BOM or
normalization) are defined once in @ref io_utf8_policy; this page covers only Terminal-specific behavior.

## Public text contract

Text arguments to `writeText()`, `writeLine()`, formatted output, styled segments, titles, and `OutputBuffer` must be complete valid UTF-8 on every
endpoint. Text and line reads return complete UTF-8 code points. Byte operations perform no UTF-8 validation.

## Real Windows console

Output converts public UTF-8 to UTF-16 and uses the native Unicode console API.

Input converts native Unicode input to UTF-8. An incomplete native surrogate sequence can be retained until a later read completes it; end-of-stream
with an incomplete sequence returns `EncodingFailed`.

Arbitrary `writeBytes()` and byte segments are unsupported for a real Win32 console because raw bytes do not define console text.

## Redirected endpoints

Redirected stdin/stdout/stderr remain byte-oriented.

- Text output writes the already-validated UTF-8 bytes unchanged.
- Byte output preserves arbitrary bytes.
- Text and line input validate redirected bytes as UTF-8.
- Byte input returns bytes unchanged.

## Buffers and segments

`OutputBuffer::appendText()` and `appendLine()` validate before mutation, so `text()` is always valid UTF-8. `writeSegments()` validates every Text and
StyledText payload before emission. Segment factories only capture views; payload lifetime rules are separate from encoding validity (@ref
terminal_segmented_writes).

## Limits and truncation

`maxReturnedBytes` counts UTF-8 bytes. Text and line reads never split a valid code point. If the limit is smaller than the next complete code point,
the operation returns `SizeLimitExceeded` rather than a partial encoding.

`wasTruncated` reports successful limit-based truncation when at least a complete permitted prefix can be returned. Incomplete UTF-8 at a terminating
end-of-stream returns `EncodingFailed`.

See @ref terminal_read_write and @ref terminal_capabilities_and_redirection.
