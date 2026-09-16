# Input test hooks

Input's Win32 test helpers are source-tree-only interfaces enabled by
`INPUT_ENABLE_TEST_HOOKS`. They are not installed and are not consumer API.

The private `engine/input/platform/win32/win32_input.h` hooks exercise the
production Unicode device-metadata conversion and HID normalization logic
without exposing Win32 device handles. Metadata conversion uses the shared
Unicode foundation; malformed UTF-16 returns an empty result. HID fixtures
provide synthetic integer ranges and usages so normalization can be checked
without enumerating physical devices.

These helpers are stateless; no reset API is required. Production builds leave
`INPUT_INTERNAL_TEST_HOOKS` disabled and do not link against them.
