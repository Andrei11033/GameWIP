@page desktop_fullscreen_monitors Fullscreen and displays

Window separates monitor identity, physical display modes, and the policy placing a Window on a display.

## Display types

Under `Desktop::Types::Display`:

- `MonitorId` is a process-local monitor identity (`isValid()`).
- `Mode` is physical resolution, millihertz refresh, color depth, and interlace state.
- Queries return `Types::Result<T>`; `value` is present only when `status.ok()`.

Opt-in via `desktop/display_info.h`:

- `Info`: monitor geometry, work area, DPI/scale, physical size, name, primary state.
- `ColorSpace` and `ColorInfo`: OS HDR/WCG/color facts; physical
  measurements are optional when the OS provides none.

## Display operations

`Desktop::Display` owns discovery and inspection. `desktop/display.h` gives `getModes()`, `getCurrentMode()`, `getPreferredMode()`;
`desktop/display_info.h` adds `getMonitors()`, `getPrimaryMonitor()`, `getMonitor()`, `getColorInfo()`.

`getColorInfo(const Window&)` is a checked convenience for the Window's display, since choosing the
native display is platform behavior; overloads are not added elsewhere for symmetry.

## Window modes

Top-level Window presentation is cached as `Types::WindowMode`:

- `Windowed` keeps ordinary desktop placement.
- `BorderlessFullscreen` fills the selected monitor without changing its physical display mode.
- `ExclusiveFullscreen` may switch to an exact requested `Types::Display::Mode`.

Callers build a `Types::WindowModeRequest` with named factories, which carry presentation concerns only: `windowed()`,
`borderlessFullscreen(monitor)`, and `exclusiveFullscreen(monitor, mode)`; the no-argument `exclusiveFullscreen()` keeps the monitor's
current physical mode. A missing monitor means current/nearest, then primary; an unknown nonzero ID is rejected.

`Types::FullscreenState` reports the active state: `requestedDisplayMode` is what was asked, `activeDisplayMode` what the monitor uses, and
`displayModeMatchedExactly` flags exact match versus fallback. `monitor` and both modes are optional (empty in windowed mode or when unreportable).

`windowed.clientSize` is logical, remains the restored windowed
size, and is distinct from physical `Display::Mode::resolution`. Windowed geometry mutations are invalid while fullscreen is active.

## Display changes

Win32 recovers fullscreen state on topology changes and queues `Types::Events::DisplayConfigurationChanged` / `MonitorChanged` /
`WindowModeChanged` as appropriate. Color changes also surface as display-configuration events; re-query
`Display::getColorInfo()`. DXGI factory lifetime: @ref desktop_lifecycle_events.
