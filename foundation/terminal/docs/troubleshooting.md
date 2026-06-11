@page terminal_troubleshooting Terminal troubleshooting

## A control returns Unsupported

Cursor movement, clearing, alternate screen, title, and bell require backend support on the selected output stream. Redirected and detached streams commonly report `Unsupported` or `NotOpen`.

Check `getOutputCapabilities()` before relying on controls.

## Styled output is plain

`StyleMode::Auto` writes plain text when the stream does not report support for the requested style. Use `StyleMode::Required` only when failure is preferable to plain output.

## Text reads fail with EncodingFailed

Text and line helpers expect valid UTF-8. Use `readBytes()` for arbitrary redirected input.

## Timed reads return no text

Inspect both `status` and `outcome`. `TimedOut` and `WouldBlock` are successful read outcomes when no data became available within the requested wait.

## Flush succeeds but a pipe consumer sees no change

Terminal writes directly to the native stdout or stderr handle. On Win32, pipe and console flush requests are successful no-ops; only a regular redirected disk file receives `FlushFileBuffers`. Flush any separate C or C++ stream buffer through the API that owns it.
