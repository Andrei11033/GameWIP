@page input_library Input

`GameWIP::Input` owns the platform-neutral input contract used to discover
devices, capture frame-based observations, and expose raw controls without
assigning application actions or bindings. It is a low-level device library:
the action layer decides how control values become gameplay behavior.

## How the library is organized

`System` owns one capture session and publishes the current device descriptors,
input events, and reusable frame results. `Platform::Win32::Attachment` connects
that session to an existing owner-thread window on Windows. Synthetic input is
available through an explicit test and tooling interface, while standard
identifiers cover the stable keyboard, mouse, and controller controls that
applications may preassign.

## Consumer manual

- @subpage input_quick_start - Open a session, attach a platform source,
  capture frames, discover devices, and inspect controls.

## Generated API reference

Use `input/input.h` for the umbrella API, @ref GameWIP::Input::System for
session ownership, and @ref GameWIP::Input::Types for device, event, frame, and
control values. Standard identifiers and lookup helpers live under
@ref GameWIP::Input::Standard and @ref GameWIP::Input::Query.

## Key behavior

- Input reports device observations and control values; it does not create
  actions, bindings, or user settings.
- Device and element identities are scoped to the current open session.
- Unknown controls remain discoverable through their device descriptors and raw
  values instead of being discarded because they lack a standard identifier.
- Capture is frame-based and preserves the event and state information needed
  by a higher-level action system.

## Dependency boundary

Input's portable API is independent of Desktop, Action, WindowManager, Renderer, UI, and the game executable.
The platform attachment owns native observation and feeds the portable `System`; it does not own application windows or action policy.

On Windows, optional integration with Desktop is available: see @ref desktop_native_interop for `Platform::Win32::attachToWindow()` which
registers Input with an existing Desktop window for seamless message routing.
