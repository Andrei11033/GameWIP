@page terminal_color_and_redirection Terminal color and redirection

This compatibility page points to the current Terminal styling and redirection contract.

Color and style emission is implemented for capable terminal streams. Redirected output remains plain in `StyleMode::Auto`; forced styling reports `Unsupported` unless the backend can honestly support the request.

Color and style behavior is documented in @ref terminal_styling.

Stream kind, capability detection, and redirection behavior are documented in @ref terminal_capabilities_and_redirection.

Current public names use `StyleMode`, `StyleCapabilities`, `StreamKind`, `TextWriteOptions`, `LineWriteOptions`, `WriteSegment`, `writeText()`, `writeLine()`, and `writeSegments()`.
