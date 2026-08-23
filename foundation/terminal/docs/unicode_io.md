@page terminal_unicode_io Unicode and byte I/O

Terminal presents UTF-8 text at its public boundary and preserves a separate raw-byte path for endpoints and callers that need arbitrary bytes.

## Public text contract

Text arguments supplied to `writeText()`, `writeLine()`, formatted output, styled segments, titles, and `OutputBuffer` are expected to contain UTF-8.

Text and line reads return complete UTF-8 code points. Invalid input observed by text helpers returns `EncodingFailed`. Byte operations perform no
UTF-8 validation.

## Real Windows console

Real-console output converts public UTF-8 to UTF-16 and uses the native Unicode console API. Invalid UTF-8 can therefore fail conversion with
`EncodingFailed` before the requested text is written.

Real-console input converts native Unicode input to UTF-8. An incomplete native surrogate sequence can be retained until a later read completes it;
end-of-stream with an incomplete sequence returns `EncodingFailed`.

Arbitrary `writeBytes()` and byte segments are unsupported for a real Win32 console because raw bytes do not define console text.

## Redirected endpoints

Redirected stdin/stdout/stderr remain byte-oriented.

- Text output writes supplied bytes without independent UTF-8 validation by the redirection path.
- Byte output preserves arbitrary bytes.
- Text and line input validate redirected bytes as UTF-8.
- Byte input returns bytes unchanged.

Therefore invalid UTF-8 may pass through a redirected text write even though the same text would fail on a real-console conversion path. Applications
requiring validation independent of endpoint kind must validate before calling Terminal.

## Buffers and segments

`OutputBuffer` and segment factories capture or append text without eagerly validating UTF-8. Text payload lifetime rules are separate from encoding
validity. Validation/conversion occurs only when required by the selected output backend.

## Limits and truncation

`maxReturnedBytes` counts UTF-8 bytes. Text and line reads never split a valid code point. If the limit is smaller than the next complete code point,
the operation returns `SizeLimitExceeded` rather than a partial encoding.

`wasTruncated` reports successful limit-based truncation when at least a complete permitted prefix can be returned. Incomplete UTF-8 at a terminating
end-of-stream returns `EncodingFailed`.

See @ref terminal_read_write and @ref terminal_capabilities_and_redirection.
