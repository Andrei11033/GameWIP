@page logger_configuration Configuration

`Logger::Types::Config` is copied by `init()`.

## Output

`output` defaults to `OutputMode::Both`. `OutputMode::None` is a successful disabled configuration. File-only startup may fall back to Console when
`fallbackToConsoleOnFileFailure` is true; `OutputMode::Both` keeps Console when File setup fails.

`logDirectory` and registered source names are UTF-8 and are validated at initialization. Invalid UTF-8 is rejected rather than repaired or
normalized.

`timestampFormat` controls displayed local timestamps and defaults to `%H:%M:%S.%f`, which preserves the built-in `HH:MM:SS.mmm` output. It uses
the platform's `strftime` directives, with Logger's `%f` extension for three-digit milliseconds; `%%` emits a literal percent sign. For example,
`%Y-%m-%d %H:%M:%S.%f` includes the local date. The format must be non-empty, valid UTF-8, at most 63 bytes, and contain no control characters
or dangling `%`. File names retain their separate collision-safe timestamp format.

```cpp
config.timestampFormat = "%Y-%m-%d %H:%M:%S.%f";
```

## Console theme and record layout

`Config::theme` controls the semantic foreground colors. By default, the timestamp and field names use muted gray, the area uses normal white,
Trace uses bright green, and unmarked message text follows the severity color. Set `theme.messageUsesSeverityColor` to `false` to use
`theme.message` instead, which is normal white by default. RGB colors are approximated to a basic terminal color when required; redirected
output remains plain. The default line format is `[HH:MM:SS.mmm][Area][LEVEL] message`, with INFO and WARN padded to the five-character
severity width. Files and debugger output use that same text layout without terminal color sequences. The custom layout template is validated and
compiled during `init()`; a per-record layout is compiled when `Record::layout()` is called.

```cpp
auto config = Logger::defaultConfig();
config.theme.timestamp = Logger::Color::rgb(130, 130, 140);
config.theme.code = Logger::Color::Magenta;
config.theme.messageUsesSeverityColor = false;
config.fieldLayout = Logger::Layouts::adaptive();
config.maxRecordFields = 8;
config.maxFieldKeyLength = 64;
config.maxFieldValueLength = 512;
```

Registered area labels can set an optional color using `defineSource(id, name, color)`. `Record::layout()` overrides the configured field
presentation for one call. Adaptive places short fields inline before the message and moves long or multiline field sets to continuation lines.

The built-in layout and custom layouts use the same template renderer. A custom compile-time template can replace the complete line layout:

```cpp
constexpr auto diagnosticLayout = Logger::makeLayout(
    "{@timestamp}[{time}]{/}{@area}[{area:<auto}]{/}{@severity}[{level:<5}]{/} {message} {fields}");
config.layout = diagnosticLayout;
```

Use `Record::layout(diagnosticLayout)` to override it for one record. Supported placeholders are `time`, `level`, `area`, `message`,
`source_file`, `source_function`, `source_line`, `thread_id`, `process_id`, and `fields`. Alignment uses `<` for left, `^` for center, and `>` for
right, followed by a minimum byte width. Centering puts any odd extra space on the right. Width is measured in UTF-8 bytes, not terminal display
columns. `auto` uses the longest registered area name or the five-character severity width. The built-in layout places the area before severity and
uses left alignment for both. Use `{@timestamp}`, `{@severity}`, `{@area}`, `{@message}`, `{@field}`, `{@code}`, `{@path}`, `{@number}`, or
`{@quoted}` to color literal text with that semantic theme role; `{/}` ends a scope. Scopes can nest up to eight levels, and marked format values
keep their own role colors inside a scope. `makeLayout()` rejects malformed placeholders and unbalanced scopes at compile time; `init()` rejects
invalid runtime-constructed templates.
`init()` copies and compiles the configured layout, and `Record::layout()` copies and compiles its template, so both remain valid after the original
view expires. Rendering executes the compiled instructions instead of reparsing the template for each output line.

## Queue and formatting

- `maxQueueSize == 0` is adjusted to the small fallback queue.
- `maxMessageLength == 0` is adjusted to the fallback message limit.
- invalid/non-finite hard-queue multipliers are adjusted to `1.0`.
- nonzero worker batch values are clamped to the hard queue limit.
- inline capacity is clamped to the message limit.
- a first queue allocation failure retries with the small fallback queue.
- `workerBatchSize == 0` means use the default and is not itself an adjustment.

These recoveries are successful initialization adjustments reported in `Types::Init::Result::adjustments`; they are not IO failures.

Undefined `OutputMode`, `Level`, or `FormatPolicy` enum values and invalid initial filters are rejected with `InvalidArgument`. Invalid `FormatPolicy`
is not silently replaced.

## `Types::Init::Result`

`status` answers whether initialization itself succeeded. `outcome` tells whether the resulting Logger is Started or Disabled. `requestedOutput` and
`effectiveOutput` expose fallback directly. `outputSetupStatus` preserves File setup and timestamp setup failures even when overall init succeeds.
That can mean Console fallback, or a degraded File logger when the log file opened but timestamp conversion reported a failure.

Use `getQueueLimits()` for authoritative effective queue/message values.
